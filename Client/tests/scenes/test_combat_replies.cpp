#include "Scenes/CombatReplies.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Combat.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorDying.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// CL9 hito 3 (04 § Pegar, el daño y la muerte): el 0x16 y el 0x9C (0x4cf200, 0x4cfce0), el 0x17 (0x4d0a30, con
// 0x4d07c0 en Blood Castle y Chaos Castle), 0x4f4a10 y 0x4f5660 sobre el mundo. Las claves, los textos y los nombres
// son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorCombatContext;
using Client::Scenes::ActorState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 4321;
constexpr std::uint16_t Monster = 0x10;
constexpr std::uint16_t NoItem = Client::Scenes::NoItemModel;
constexpr std::uint32_t OwnRecordDraws = 0xf08;
constexpr float DeathFrame = std::bit_cast<float>(0x3a83126fu);

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// Los esqueletos de 0x4ea030 para los monstruos de los tests (Player.bmd va aparte, en el contexto).
class CombatModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Monster || model == 0x172 || model == 0x19e || model == 0 ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

class CombatRepliesTest : public ::testing::Test
{
protected:
    CombatRepliesTest()
    {
        Runtime().map = 0;
        Runtime().hero = HeroIndex;
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        m_world->OwnCharacter().life = 50;
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    [[nodiscard]] ActorState& Hero()
    {
        return m_world->Actors()[HeroIndex];
    }
    [[nodiscard]] ActorState& Actor(std::size_t index)
    {
        return m_world->Actors()[index];
    }
    // Un monstruo vivo con `key` en `index`.
    ActorState& MonsterAt(std::size_t index, std::uint16_t key)
    {
        ActorState& actor = Actor(index);
        actor.alive = true;
        actor.type = Monster;
        actor.key = key;
        actor.byte88 = 2;
        return actor;
    }
    // Un jugador vivo, sin armas ni mascota, con `key` en `index`.
    ActorState& PlayerAt(std::size_t index, std::uint16_t key, std::uint8_t characterClass = 0)
    {
        ActorState& actor = Actor(index);
        actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.key = key;
        actor.byte88 = 1;
        return actor;
    }
    [[nodiscard]] ActorCombatContext Combat()
    {
        return ActorCombatContext{.world = *m_world,
                                  .random = m_random,
                                  .playerSkeleton = &m_playerSkeleton,
                                  .models = &m_models,
                                  .items = &m_items};
    }
    // Con la tabla de sonidos `sounds` para el modelo Monster (la de 0x612c80) y ninguna para los demás.
    [[nodiscard]] ActorCombatContext CombatWithSounds(const Client::Scenes::ModelSounds& sounds)
    {
        ActorCombatContext combat = Combat();
        combat.modelSounds = [sounds](std::uint16_t model) -> std::optional<Client::Scenes::ModelSounds>
        {
            if (model != Monster)
                return std::nullopt;
            return sounds;
        };
        return combat;
    }
    [[nodiscard]] Client::Scenes::ChatReceiveContext Chat()
    {
        return Client::Scenes::ChatReceiveContext{
            .world = *m_world, .random = m_random, .measure = &m_letters, .texts = &m_texts};
    }
    void UseTerrain()
    {
        m_world->Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    }
    std::uint16_t& Wall(std::uint8_t x, std::uint8_t y)
    {
        return m_world->Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(x, y)];
    }
    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }
    // Una copia del rand() después de `count`, para sacar los mismos valores que el código.
    [[nodiscard]] static CrtRandom DrawnAfter(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected;
    }

    static constexpr std::size_t HeroIndex = 5;
    static constexpr std::uint16_t HeroKey = 0x2a;
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Render::Models::Skeleton m_playerSkeleton = Client::Tests::PlayerActionsSkeleton();
    CombatModels m_models;
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable({{486, "Experiencia %d"}});
    LetterMeasure m_letters;
};

[[nodiscard]] bool HasError(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().find(address) != std::string::npos;
}

// 0x4cf200 sin el bit 15: el héroe anota el muerto (+0x380 en 2, el daño y el lugar), el número del daño sale con la
// luz (1; 0,6; 0) y la escala 15, el muerto queda muriéndose y la experiencia se suma en el registro propio (3848
// rand() al cerrarlo) y va al chat con el texto 486.
TEST_F(CombatRepliesTest, AKillWithoutShockMarksTheVictimOnTheHeroAndAddsTheExperience)
{
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1000.0f, 2000.0f, 30.0f};
    dead.movement = 1;
    m_world->OwnCharacter().experience = 1000;
    const Client::Protocol::Combat::KillExperience reply{.key = 0x123, .shock = false, .amount = 500, .damage = 77};

    const auto applied = Client::Scenes::ApplyKillExperience(reply, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    EXPECT_EQ(Hero().byte380, 2);
    EXPECT_EQ(Hero().word382, 77);
    EXPECT_EQ(Hero().attackTarget, 12);
    EXPECT_EQ(dead.dying, 1);
    EXPECT_EQ(dead.movement, 0);
    EXPECT_EQ(dead.animation.action, 0);
    const Client::Scenes::DamageNumberState& number = m_world->DrawTail().damage[0];
    EXPECT_TRUE(number.alive);
    EXPECT_EQ(number.number, 77);
    EXPECT_EQ(number.position, glm::vec3(1000.0f, 2000.0f, 170.0f));
    EXPECT_EQ(number.light, glm::vec3(1.0f, 0.6f, 0.0f));
    EXPECT_EQ(number.scale, 15.0f);
    EXPECT_EQ(number.speed, 10.0f);
    EXPECT_TRUE(number.rise);
    EXPECT_EQ(m_world->OwnCharacter().experience, 1500u);
    ASSERT_EQ(Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(Runtime().chat.log.entries[0].text, "Experiencia 500");
    EXPECT_EQ(Runtime().chat.log.entries[0].type, Client::UI::SystemMessage);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4cfca6: con experiencia 0 no hay mensaje, pero el registro propio igual se abre y se cierra.
TEST_F(CombatRepliesTest, AKillWithoutExperienceStillClosesTheOwnRecordAndAddsNoMessage)
{
    MonsterAt(12, 0x123);
    const auto applied =
        Client::Scenes::ApplyKillExperience({.key = 0x123, .shock = false, .amount = 0, .damage = 3}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(Runtime().chat.log.entries.empty());
    EXPECT_EQ(m_world->OwnCharacter().experience, 0u);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4cfce0, el PMSG_KILLPLAYER_EXT de la recompensa de Blood Castle (0x49af90): la experiencia de 32 bits, como la
// arma el GameServer (dos WORD), entra entera en el registro propio.
TEST_F(CombatRepliesTest, TheExtendedKillAddsTheWholeThirtyTwoBitExperience)
{
    MonsterAt(30, 0x456);
    m_world->OwnCharacter().experience = 7;
    const auto applied = Client::Scenes::ApplyKillExperience(
        {.key = 0x456, .shock = false, .amount = 0x10000u + 20000u, .damage = 9}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(m_world->OwnCharacter().experience, 7u + 0x10000u + 20000u);
    EXPECT_EQ(Runtime().chat.log.entries.at(0).text, "Experiencia 85536");
    EXPECT_EQ(Hero().attackTarget, 30);
}

// Con el bit 15, 0x4f5660 sobre el muerto: un elfo de Devias toma la acción 0xc3, el sonido 0x6e y el frame 0,001.
// El héroe no anota nada.
TEST_F(CombatRepliesTest, AShockKillsAnotherPlayerWithTheDeathActionAndSound)
{
    Runtime().map = 2;
    ActorState& dead = PlayerAt(20, 0x55, 2);
    const auto applied =
        Client::Scenes::ApplyKillExperience({.key = 0x55, .shock = true, .amount = 10, .damage = 4}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(dead.animation.action, 0xc3);
    EXPECT_EQ(dead.animation.frame, DeathFrame);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x6e));
    EXPECT_EQ(dead.dying, 1);
    EXPECT_EQ(Hero().byte380, 0);
    EXPECT_EQ(Hero().attackTarget, 0);
    EXPECT_EQ(m_world->OwnCharacter().life, 50);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// 0x4f5a09..0x4f5ae8: el sonido de morir del jugador por la clase, y Fenrir lo pisa con el 0x278.
TEST_F(CombatRepliesTest, ThePlayerDeathSoundFollowsTheClassAndFenrir)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t pet;
        std::int32_t sound;
    };
    for (const Case probe : {Case{0, NoItem, 0x6c}, Case{1, NoItem, 0x6c}, Case{2, NoItem, 0x6e}, Case{3, NoItem, 0x6c},
                             Case{4, NoItem, 0x1c7}, Case{0x12, NoItem, 0x6e}, Case{4, 0x1c37, 0x278}})
    {
        Runtime().lastSoundId.reset();
        ActorState& dead = PlayerAt(20, 0x55, probe.characterClass);
        dead.pet.model = probe.pet;
        ASSERT_TRUE(Client::Scenes::SetActorDie(dead, Combat()).HasValue());
        EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(probe.sound)) << int{probe.characterClass};
    }
}

// La muerte del héroe abre el registro propio para poner la vida en 0 (3848 rand()) y después el de la experiencia
// los saca otra vez.
TEST_F(CombatRepliesTest, AShockOnTheHeroZeroesTheOwnLifeInsideTheOwnRecord)
{
    const auto applied = Client::Scenes::ApplyKillExperience({.key = HeroKey, .shock = true, .amount = 1, .damage = 1},
                                                             Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(m_world->OwnCharacter().life, 0);
    EXPECT_EQ(Hero().animation.action, 0xc3);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x6c));
    EXPECT_EQ(Hero().dying, 1);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws));
    EXPECT_EQ(Runtime().ownRecordDepth, 0);
}

// En Chaos Castle (18 a 23) el sonido de morir es rand() & 1 + 0x5e, para todos.
TEST_F(CombatRepliesTest, InChaosCastleTheDeathSoundIsDrawn)
{
    Runtime().map = 18;
    ActorState& dead = PlayerAt(20, 0x55, 2);
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t sound = (expected.Next() & 1) + 0x5e;
    ASSERT_TRUE(
        Client::Scenes::ApplyKillExperience({.key = 0x55, .shock = true, .amount = 0, .damage = 1}, Combat(), Chat())
            .HasValue());
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(sound));
    EXPECT_EQ(dead.animation.frame, DeathFrame);
    EXPECT_EQ(m_random.State(), After(1 + OwnRecordDraws));
}

// El monstruo que muere con el frame en 0 pide el sonido de la WORD 4 de su tabla (0x5769ad8 + tipo · 0xe0 + 0xb2),
// con signo, más 5 con el subtipo 9 y más 0xd2, sin rand(); con 0xffff, o sin la tabla, ninguno. El frame queda en
// 0,001 igual. El que ya estaba en la acción 6 con otro frame no pide sonido.
TEST_F(CombatRepliesTest, AMonsterDeathSoundComesFromItsTableOnlyWithTheFrameAtZero)
{
    struct Case
    {
        std::int16_t word;
        std::int32_t subtype;
        std::optional<std::int32_t> sound;
    };
    for (const Case& tested : {Case{0x7d, 0, 0x7d + 0xd2}, Case{0x7d, 9, 0x7d + 5 + 0xd2}, Case{-1, 0, std::nullopt},
                               Case{-3, 0, -3 + 0xd2}})
    {
        SCOPED_TRACE(tested.word);
        Runtime().lastSoundId.reset();
        ActorState& dead = MonsterAt(12, 0x123);
        dead.subtype = tested.subtype;
        dead.animation = {};
        Client::Scenes::ModelSounds sounds{};
        sounds.fill(0x11);
        sounds[4] = tested.word;
        const auto applied = Client::Scenes::ApplyKillExperience(
            {.key = 0x123, .shock = true, .amount = 5, .damage = 1}, CombatWithSounds(sounds), Chat());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(dead.animation.action, 6);
        EXPECT_EQ(dead.animation.frame, DeathFrame);
        EXPECT_EQ(Runtime().lastSoundId, tested.sound);
    }

    Runtime().lastSoundId.reset();
    MonsterAt(14, 0x125);
    ASSERT_TRUE(
        Client::Scenes::ApplyKillExperience({.key = 0x125, .shock = true, .amount = 5, .damage = 1}, Combat(), Chat())
            .HasValue());
    EXPECT_EQ(Actor(14).animation.action, 6);
    EXPECT_EQ(Actor(14).animation.frame, DeathFrame);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());

    ActorState& dying = MonsterAt(13, 0x124);
    dying.animation.action = 6;
    dying.animation.frame = 2.0f;
    const auto applied =
        Client::Scenes::ApplyKillExperience({.key = 0x124, .shock = true, .amount = 5, .damage = 1}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(dying.animation.frame, 2.0f);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(dying.dying, 1);
    // Cinco cierres del registro propio, uno por experiencia, y ningún rand() de los sonidos.
    EXPECT_EQ(m_random.State(), After(6 * OwnRecordDraws));
}

// El 0x9C que manda el GameServer 0.97 (0x49af90) lleva la clave 0xFFFF: 0x7fff con el bit 15. Nadie tiene la 0x7fff y
// 0x5110d0 devuelve el lugar 400, el de tipo 0, que muere sin sonido (la tabla del modelo 0 no está) y suma la
// experiencia (Q-C1160, P-C252).
TEST_F(CombatRepliesTest, TheBloodCastleRewardLandsOnSlotFourHundred)
{
    const auto applied = Client::Scenes::ApplyKillExperience({.key = 0x7fff, .shock = true, .amount = 100, .damage = 0},
                                                             Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(400).animation.action, 6);
    EXPECT_EQ(Actor(400).dying, 1);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_world->OwnCharacter().experience, 100u);
}

// 0x4d0a30 fuera de los castillos y sin skill de empuje: muriéndose, quieto, con la skill anotada y sin rand().
TEST_F(CombatRepliesTest, AnOrdinaryDeathOnlyMarksTheActor)
{
    for (const std::int32_t map : {0, 2})
    {
        Runtime().map = map;
        ActorState& dead = MonsterAt(12, 0x123);
        dead.movement = 1;
        dead.byte1a1 = 1;
        ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0x10, .killer = 0}, Combat()).HasValue());
        EXPECT_EQ(dead.dying, 1);
        EXPECT_EQ(dead.movement, 0);
        EXPECT_EQ(dead.deathSkill, 0x10);
        EXPECT_EQ(dead.byte1a1, 0);
        EXPECT_EQ(dead.floatD0, 0.0f);
    }
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4d0ac1..0x4d0b98: la skill 0x28 (y la 0x3b) lo empuja con dos rand() y lo gira hacia quien lo mató (0x4e93f0).
TEST_F(CombatRepliesTest, APushingSkillThrowsTheDeadAwayFromTheKiller)
{
    for (const std::uint8_t skill : {std::uint8_t{0x28}, std::uint8_t{0x3b}})
    {
        m_random = CrtRandom(Seed);
        ActorState& dead = MonsterAt(12, 0x123);
        dead.position = {1000.0f, 2000.0f, 0.0f};
        dead.angles = {3.0f, 4.0f, 5.0f};
        dead.floatCc = 9.0f;
        ActorState& killer = PlayerAt(20, 0x55);
        killer.position = {1300.0f, 2400.0f, 0.0f};
        ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = skill, .killer = 0x55}, Combat()).HasValue());

        CrtRandom expected = DrawnAfter(0);
        const std::int32_t first = expected.Next() % 5;
        const std::int32_t second = expected.Next() % 15;
        EXPECT_EQ(dead.byte1a1, 1);
        EXPECT_EQ(dead.byte398, 0);
        EXPECT_EQ(dead.floatCc, 0.0f);
        EXPECT_EQ(dead.floatD8, static_cast<float>((first + 10.0) * static_cast<double>(0.1f)));
        EXPECT_EQ(dead.floatD4, 0.0f);
        EXPECT_EQ(dead.floatD0, static_cast<float>(second + 40.0));
        EXPECT_EQ(dead.pushOrigin, dead.position);
        const auto angle = static_cast<float>(Client::Render::Models::CreateAngle(1000.0f, 2000.0f, 1300.0f, 2400.0f));
        EXPECT_EQ(dead.angles.z, angle);
        EXPECT_EQ(dead.angle30, angle);
        EXPECT_EQ(dead.headTurn.angles.x, 3.0f);
        EXPECT_EQ(dead.headTurn.angles.y, 4.0f);
        EXPECT_EQ(m_random.State(), After(2));
    }
}

// El 17 de otro jugador con la skill 0x3b y después su paso de morirse (0x4fb9e0, CL9 hito 3, fase 2a): hasta el
// paso 14, +0xd0 baja +0xd8, +0xd8 sube 1 y 0x51fd20 lo corre por el rumbo hacia quien lo mató; sin rand() fuera del
// mapa 7. Desde el 15 queda donde llegó.
TEST_F(CombatRepliesTest, AnotherPlayerPushedToDeathFliesThroughItsDyingSteps)
{
    ActorState& dead = PlayerAt(20, 0x77);
    dead.position = {1000.0f, 2000.0f, 0.0f};
    ActorState& killer = PlayerAt(21, 0x55);
    killer.position = {1300.0f, 2000.0f, 0.0f};
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x77, .skill = 0x3b, .killer = 0x55}, Combat()).HasValue());
    ASSERT_EQ(dead.dying, 1);
    const std::uint32_t afterTheDeath = m_random.State();
    const Client::Scenes::ActorDyingContext dying{.map = 0, .hero = false, .random = m_random};

    const float d0 = dead.floatD0;
    const float d8 = dead.floatD8;
    ASSERT_TRUE(Client::Scenes::StepActorDying(dead, 20, dying).HasValue());
    EXPECT_EQ(dead.dyingTime, 0.02f);
    EXPECT_EQ(dead.floatD0, static_cast<float>(static_cast<double>(d0) - static_cast<double>(d8)));
    EXPECT_EQ(dead.floatD8, d8 + 1.0f);
    EXPECT_NE(dead.position, glm::vec3(1000.0f, 2000.0f, 0.0f));
    EXPECT_TRUE(dead.alive);

    for (std::uint8_t step = 2; step <= 14; ++step)
    {
        dead.dying = step;
        ASSERT_TRUE(Client::Scenes::StepActorDying(dead, 20, dying).HasValue());
    }
    EXPECT_EQ(dead.floatD0, 0.0f);
    const glm::vec3 landed = dead.position;
    dead.dying = 15;
    ASSERT_TRUE(Client::Scenes::StepActorDying(dead, 20, dying).HasValue());
    EXPECT_EQ(dead.position, landed);
    EXPECT_EQ(m_random.State(), afterTheDeath);
}

// 0x4d07c0 en Blood Castle (11): sobre el TerrainWall 0x20 lo empujan del puente hacia el lado de la vecina con el
// bit 8, con cinco rand().
TEST_F(CombatRepliesTest, OnTheBloodCastleBridgeTheDeadIsPushedTowardsTheFall)
{
    Runtime().map = 11;
    UseTerrain();
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1250.0f, 2350.0f, 0.0f};
    Wall(12, 23) = 0x20;
    Wall(13, 23) = 0x08;
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());

    CrtRandom expected = DrawnAfter(0);
    const std::int32_t e4 = expected.Next() % 10;
    const std::int32_t d8 = expected.Next() % 20;
    const std::int32_t push = expected.Next() % 10;
    const std::int32_t cc = expected.Next() % 6;
    const std::int32_t side = (-expected.Next()) % 2;
    EXPECT_EQ(dead.byte1a1, 1);
    EXPECT_EQ(dead.floatE4, static_cast<float>(e4 + 10.0));
    EXPECT_EQ(dead.floatD8, static_cast<float>(d8 + 20.0));
    EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, -static_cast<float>(push + 85.0)));
    EXPECT_EQ(dead.angles.z, dead.push.z);
    EXPECT_EQ(dead.floatCc, static_cast<float>(cc + 8.0));
    EXPECT_EQ(dead.floatD4, 5.0f);
    EXPECT_EQ(dead.floatD0, static_cast<float>(side + 13.0));
    EXPECT_EQ(dead.pushOrigin, dead.position);
    EXPECT_EQ(m_random.State(), After(5));

    // Con la vecina de la izquierda, hacia el otro lado.
    m_random = CrtRandom(Seed);
    Wall(13, 23) = 0;
    Wall(11, 23) = 0x08;
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, static_cast<float>(push + 85.0)));
}

// 0x4d0848..0x4d08dc: en Chaos Castle (18) sobre el TerrainWall 8 cae al vacío, con el sonido 0x60 y dos rand().
TEST_F(CombatRepliesTest, InChaosCastleTheDeadFallsIntoTheVoid)
{
    Runtime().map = 18;
    UseTerrain();
    ActorState& dead = MonsterAt(12, 0x123);
    dead.position = {1250.0f, 2350.0f, 0.0f};
    dead.floatD8 = 1.0f;
    Wall(12, 23) = 0x08;
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0x28, .killer = 0}, Combat()).HasValue());

    CrtRandom expected = DrawnAfter(0);
    const std::int32_t e4 = expected.Next() % 10;
    const std::int32_t drop = expected.Next() % 3;
    EXPECT_EQ(dead.byte1a1, 1);
    EXPECT_EQ(dead.floatE4, static_cast<float>(e4 + 10.0));
    EXPECT_EQ(dead.floatD4, 1.0f);
    EXPECT_EQ(dead.floatD8, static_cast<float>(1.0 - (drop + 3.0)));
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x60));
    EXPECT_EQ(m_random.State(), After(2));

    // El mismo TerrainWall en Blood Castle no lo hace caer; la skill tampoco empuja en los castillos.
    m_random = CrtRandom(Seed);
    Runtime().map = 11;
    Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0x28, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(dead.byte1a1, 0);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), After(0));
}

// 0x4d0a0c..0x4d0a25: el héroe que muere en un castillo devuelve el objeto del evento a 0x40ba20.
TEST_F(CombatRepliesTest, TheHeroDyingInACastleResetsTheEventObject)
{
    Runtime().map = 11;
    UseTerrain();
    Client::Scenes::EventMapObject object;
    object.field10 = 5;
    object.field14 = {1, 2, 3, 4};
    Runtime().eventObject = object;
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = HeroKey, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(Runtime().eventObject->field10, 0);
    EXPECT_EQ(Runtime().eventObject->field14, (std::array<std::int32_t, 4>{-1, -1, -1, -1}));
    EXPECT_EQ(Hero().dying, 1);

    // Otro que muere no lo toca.
    Runtime().eventObject = object;
    MonsterAt(12, 0x123);
    ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat()).HasValue());
    EXPECT_EQ(Runtime().eventObject->field10, 5);
}

// Sin los atributos del mundo cargado, 0x4d07c0 no tiene TerrainWall que leer.
TEST_F(CombatRepliesTest, ACastleDeathWithoutAttributesCuts)
{
    Runtime().map = 23;
    MonsterAt(12, 0x123);
    const auto cut = Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat());
    EXPECT_TRUE(HasError(cut, "0x4d07ff"));
}

// 0x4f4a10 sobre un jugador: 0x4f3f80 primero (3848 rand()), la acción por las armas y el sonido con el frame en 0.
TEST_F(CombatRepliesTest, AnUnarmedPlayerPunchesWithoutSound)
{
    ActorState& player = PlayerAt(20, 0x55);
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x22);
    EXPECT_EQ(player.attackCount, 1);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));
}

// Una espada sin la marca +0x1e alterna 0x23 y 0x24 por +0x38f & 1 y el sonido sale de rand() & 1 + 0x3c; con la
// marca, 0x27 + (+0x38f % 3).
TEST_F(CombatRepliesTest, ASwordAlternatesByTheAttackCountAndDrawsItsSound)
{
    ActorState& player = PlayerAt(20, 0x55);
    player.hands[0].model = 0x213;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x23);
    CrtRandom expected = DrawnAfter(OwnRecordDraws);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>((expected.Next() & 1) + 0x3c));
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws + 1));

    // 0x4ea030 vuelve el frame a 0 al cambiar de acción: la segunda también pide sonido.
    player.animation.frame = 1.0f;
    Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x24);
    EXPECT_EQ(player.attackCount, 2);
    EXPECT_TRUE(Runtime().lastSoundId.has_value());

    m_items = Client::Tests::ItemsMarking({0x213});
    player.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x27 + 2);
}

// Un arco en la izquierda tira con 0x2e (0x30 con alas) y suena 0x55, sin sortear.
TEST_F(CombatRepliesTest, ABowShootsByTheWingsAndSoundsWithoutDrawing)
{
    ActorState& player = PlayerAt(20, 0x55, 2);
    player.hands[1].model = 0xa12;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x2e);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x55));
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    player.wings.model = 0x1a00;
    player.animation.frame = 0.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x30);
}

// 0x4f4a4e..0x4f4af0: con Fenrir fuera de la zona segura, el Dark Lord pisa la acción de Fenrir con la 0x59; en la
// zona segura pega a pie.
TEST_F(CombatRepliesTest, OnFenrirTheDarkLordOverridesTheAction)
{
    ActorState& player = PlayerAt(20, 0x55, 4);
    player.pet.model = 0x1c37;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x59);
    EXPECT_EQ(player.animation.previousAction, 0x56);

    ActorState& knight = PlayerAt(21, 0x56, 1);
    knight.pet.model = 0x1c37;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(knight, Combat()).HasValue());
    EXPECT_EQ(knight.animation.action, 0x56);

    ActorState& safe = PlayerAt(22, 0x57, 4);
    safe.pet.model = 0x1c37;
    safe.safeZone = 1;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(safe, Combat()).HasValue());
    EXPECT_EQ(safe.animation.action, 0x22);
}

// 0x4f4fd7..0x4f502f: el 0x172 sortea rand() & 7 y, de 3 en adelante, rand() & 1 + 3.
TEST_F(CombatRepliesTest, TheModel172DrawsItsAttack)
{
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t first = expected.Next() & 7;
    std::uint32_t draws = 1;
    std::int32_t action = first == 0 ? 9 : 8;
    if (first >= 3)
    {
        action = (expected.Next() & 1) + 3;
        ++draws;
    }
    // Ya en esa acción y con el frame corrido, para no pedir el sonido de su tabla (CL10).
    ActorState& monster = MonsterAt(12, 0x123);
    monster.type = 0x172;
    monster.animation.action = static_cast<std::uint8_t>(action);
    monster.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, action);
    EXPECT_EQ(monster.animation.frame, 1.0f);
    EXPECT_EQ(monster.attackCount, 1);
    EXPECT_EQ(m_random.State(), After(draws));
}

// 0x4f5034..0x4f50b2: el monstruo común pega con 3 si +0x38f % 3 es 0 y con 4 si no, y suma uno más en 0x4f50b2;
// con el frame en 0 (al cambiar de acción, 0x4ea030 lo vuelve a 0) el sonido es la WORD 2 o la 3 de su tabla por
// rand() % 2, más 0xd2 (y 5 con el subtipo 9); con la WORD 2 en −1, ni sonido ni rand(); sin la tabla, el rand() y
// ningún sonido (P-C253).
TEST_F(CombatRepliesTest, AnOrdinaryMonsterCountsTwiceAndSoundsFromItsTable)
{
    ActorState& monster = MonsterAt(12, 0x123);
    monster.animation.action = 3;
    monster.animation.frame = 1.5f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, 3);
    EXPECT_EQ(monster.attackCount, 2);
    monster.animation.action = 4;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(monster.animation.action, 4);
    EXPECT_EQ(monster.animation.frame, 1.5f);
    EXPECT_EQ(monster.attackCount, 4);
    EXPECT_EQ(m_random.State(), After(0));

    ActorState& still = MonsterAt(13, 0x124);
    ASSERT_TRUE(Client::Scenes::SetActorAttack(still, Combat()).HasValue());
    EXPECT_EQ(still.animation.action, 3);
    EXPECT_EQ(still.attackCount, 2);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), After(1));

    Client::Scenes::ModelSounds sounds{};
    sounds.fill(-1);
    sounds[2] = 0x7c;
    sounds[3] = -0x10;
    CrtRandom expected = DrawnAfter(1);
    for (const std::int32_t subtype : {0, 9, 0})
    {
        SCOPED_TRACE(subtype);
        ActorState& next = MonsterAt(14, 0x125);
        next.subtype = subtype;
        next.animation = {};
        const std::int32_t drawn = expected.Next() % 2;
        ASSERT_TRUE(Client::Scenes::SetActorAttack(next, CombatWithSounds(sounds)).HasValue());
        EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(sounds[static_cast<std::size_t>(2 + drawn)] +
                                                                     (subtype == 9 ? 5 : 0) + 0xd2));
    }
    EXPECT_EQ(m_random.State(), expected.State());

    Runtime().lastSoundId.reset();
    sounds[2] = -1;
    ActorState& silent = MonsterAt(15, 0x126);
    ASSERT_TRUE(Client::Scenes::SetActorAttack(silent, CombatWithSounds(sounds)).HasValue());
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(m_random.State(), expected.State());
}

// Un jugador con el subtipo 0xd1 a 0xd3 pide rand() & 1 + 0x3c y después el de la tabla del modelo 0x20a, que nadie
// escribe: MuRet no la tiene y saca el segundo rand() sin pedir otro sonido (P-C253).
TEST_F(CombatRepliesTest, APlayerWithAModelSoundSubtypeDrawsTwice)
{
    ActorState& player = PlayerAt(20, 0x55);
    player.subtype = 0xd2;
    CrtRandom expected = DrawnAfter(OwnRecordDraws);
    const std::int32_t sound = (expected.Next() & 1) + 0x3c;
    static_cast<void>(expected.Next());
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x22);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(sound));
    EXPECT_EQ(m_random.State(), expected.State());
}

// Los ayudantes de 0x4f5034 que no están portados cortan con su dirección; el 0x19e pone +0x110 en 3 y la acción 5.
TEST_F(CombatRepliesTest, TheMonsterHelpersCutOrSetTheirAction)
{
    ActorState& kalima = MonsterAt(12, 0x123);
    kalima.word376 = 0x91;
    EXPECT_TRUE(HasError(Client::Scenes::SetActorAttack(kalima, Combat()), "0x43a2f0"));

    Runtime().map = 0x22;
    ActorState& crywolf = MonsterAt(13, 0x124);
    crywolf.word376 = 0x154;
    EXPECT_TRUE(HasError(Client::Scenes::SetActorAttack(crywolf, Combat()), "0x437580"));

    Runtime().map = 0;
    ActorState& elsewhere = MonsterAt(14, 0x125);
    elsewhere.word376 = 0x154;
    elsewhere.animation.action = 3;
    elsewhere.animation.frame = 1.0f;
    EXPECT_TRUE(Client::Scenes::SetActorAttack(elsewhere, Combat()).HasValue());

    ActorState& special = MonsterAt(15, 0x126);
    special.type = 0x19e;
    special.animation.action = 5;
    special.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(special, Combat()).HasValue());
    EXPECT_EQ(special.byte110, 3);
    EXPECT_EQ(special.animation.action, 5);
    EXPECT_EQ(special.attackCount, 1);

    ActorState& effect = MonsterAt(16, 0x127);
    effect.type = 0x27;
    EXPECT_TRUE(HasError(Client::Scenes::SetActorAttack(effect, Combat()), "0x4f4f5b"));
}
} // namespace
