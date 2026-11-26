#include "Models/IModelLibrary.h"
#include "Scenes/CombatReplies.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/ItemTable.h"
#include "Tables/MonsterSkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/TextTable.h"
#include "Protocol/Combat/Combat.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
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
#include "Sound/MonsterSounds.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "World/ModelEffects.h"
#include "World/WizardModelEffects.h"

#include "support/PlayerActors.h"
#include "support/SceneDataFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// CL9 hito 3 (04 § Pegar, el daño y la muerte): el 0x16 y el 0x9C (0x4cf200, 0x4cfce0), el 0x17 (0x4d0a30, con
// 0x4d07c0 en Blood Castle y Chaos Castle), 0x4f4a10 y 0x4f5660 sobre el mundo. Las claves, los textos y los nombres
// son inventados.
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::ActorCombatContext;
using Client::Scenes::ActorState;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 4321;
constexpr std::uint16_t Monster = 0x10;
constexpr std::uint16_t NoItem = Client::Scenes::NoItemModel;
constexpr std::uint32_t OwnRecordDraws = 0xf08;
constexpr float DeathFrame = std::bit_cast<float>(0x3a83126fu);
constexpr float UnorderedFrame = std::bit_cast<float>(0x7fc00000u);

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
class CombatModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t /*model*/) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == Monster || model == 0x172 || model == 0x19e || model == 0 ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t /*model*/) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
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
    [[nodiscard]] ActorCombatContext CombatWithSounds(const Client::Sound::ModelSounds& sounds)
    {
        ActorCombatContext combat = Combat();
        combat.modelSounds = [sounds](std::uint16_t model) -> std::optional<Client::Sound::ModelSounds>
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
    Client::Models::Skeleton m_playerSkeleton = Client::Tests::PlayerActionsSkeleton();
    CombatModels m_models;
    Client::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{486, "Experiencia %d"}});
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
// con signo, más 5 con el subtipo 9 y más 0xd2, sin rand(); con 0xffff, ninguno; sin la tabla, la WORD en 0 de
// 0x60dc80: el 0xd2 (0xd7 con el subtipo 9). El frame queda en 0,001 igual. El que ya estaba en la acción 6 con otro
// frame no pide sonido.
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
        Client::Sound::ModelSounds sounds{};
        sounds.fill(0x11);
        sounds[4] = tested.word;
        const auto applied = Client::Scenes::ApplyKillExperience(
            {.key = 0x123, .shock = true, .amount = 5, .damage = 1}, CombatWithSounds(sounds), Chat());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(dead.animation.action, 6);
        EXPECT_EQ(dead.animation.frame, DeathFrame);
        EXPECT_EQ(Runtime().lastSoundId, tested.sound);
    }

    for (const std::int32_t subtype : {0, 9})
    {
        SCOPED_TRACE(subtype);
        Runtime().lastSoundId.reset();
        Actor(14) = ActorState{};
        MonsterAt(14, 0x125).subtype = subtype;
        ASSERT_TRUE(Client::Scenes::ApplyKillExperience({.key = 0x125, .shock = true, .amount = 5, .damage = 1},
                                                        Combat(), Chat())
                        .HasValue());
        EXPECT_EQ(Actor(14).animation.action, 6);
        EXPECT_EQ(Actor(14).animation.frame, DeathFrame);
        EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(subtype == 9 ? 0xd7 : 0xd2));
    }

    Runtime().lastSoundId.reset();

    ActorState& dying = MonsterAt(13, 0x124);
    dying.animation.action = 6;
    dying.animation.frame = 2.0f;
    const auto applied =
        Client::Scenes::ApplyKillExperience({.key = 0x124, .shock = true, .amount = 5, .damage = 1}, Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(dying.animation.frame, 2.0f);
    EXPECT_FALSE(Runtime().lastSoundId.has_value());
    EXPECT_EQ(dying.dying, 1);
    // Siete cierres del registro propio, uno por experiencia, y ningún rand() de los sonidos.
    EXPECT_EQ(m_random.State(), After(7 * OwnRecordDraws));
}

// 0x4f58a7, 0x4f5942 y 0x4f599e apagan el byte vivo antes de intentar los efectos; con el pool lleno no se crean y
// siguen el sonido 0x7c, el gate del frame y 0x4f5a09. No cambia la acción ni se gastan rand() salvo en Chaos Castle.
TEST_F(CombatRepliesTest, SpecialEffectDeathsContinueWhenTheModelEffectPoolIsFull)
{
    struct Case
    {
        std::uint16_t model;
        std::int32_t subtype;
        std::uint8_t characterClass;
        std::optional<std::int16_t> deathSound;
    };
    const std::array cases{
        Case{0x20a, 0xd1, 0, std::int16_t{-1}}, Case{0x20a, 0xd2, 2, std::int16_t{0x7d}},
        Case{0x20a, 0xd3, 4, std::nullopt},     Case{0x16b, 9, 1, std::int16_t{0x7d}},
        Case{0x170, 0, 3, std::nullopt},
    };
    constexpr std::array<float, 3> Frames{1.0f, 0.0f, UnorderedFrame};
    constexpr std::array<std::int32_t, 7> Maps{0, 18, 19, 20, 21, 22, 23};

    for (const std::int32_t map : Maps)
    {
        for (const Case& tested : cases)
        {
            for (std::size_t frameIndex = 0; frameIndex < Frames.size(); ++frameIndex)
            {
                SCOPED_TRACE(map);
                SCOPED_TRACE(tested.model);
                SCOPED_TRACE(tested.subtype);
                SCOPED_TRACE(frameIndex);
                Runtime().map = map;
                Runtime().ownRecordDepth = 0;
                Runtime().lastSoundId.reset();
                Runtime().modelSounds.clear();
                if (tested.deathSound)
                {
                    Client::Sound::ModelSounds sounds{};
                    sounds.fill(0);
                    sounds[4] = *tested.deathSound;
                    Runtime().modelSounds[tested.model] = sounds;
                }

                auto effectPool = m_world->Map().effects.Effects();
                for (Client::World::ModelEffect& effect : effectPool)
                    effect.alive = true;

                ActorState& dead = Actor(20);
                dead = ActorState{};
                dead.alive = true;
                dead.type = tested.model;
                dead.subtype = tested.subtype;
                dead.characterClass = tested.characterClass;
                dead.animation.action = 7;
                dead.animation.frame = Frames[frameIndex];
                const std::uint32_t oldFrame = std::bit_cast<std::uint32_t>(dead.animation.frame);
                CrtRandom expected = m_random;
                std::optional<std::int32_t> sound = 0x7c;
                std::uint32_t expectedFrame = oldFrame;
                if (frameIndex != 0)
                {
                    expectedFrame = std::bit_cast<std::uint32_t>(DeathFrame);
                    if (map >= 18 && map <= 23)
                    {
                        sound = static_cast<std::int32_t>((expected.Next() & 1) + 0x5e);
                    }
                    else
                    {
                        const std::int16_t word = tested.deathSound.value_or(0);
                        if (word != -1)
                            sound = word + (tested.subtype == 9 ? 5 : 0) + 0xd2;
                    }
                }

                const auto died = Client::Scenes::SetActorDie(dead, Combat());
                ASSERT_TRUE(died.HasValue()) << died.Error();
                EXPECT_FALSE(dead.alive);
                EXPECT_EQ(dead.animation.action, 7);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(dead.animation.frame), expectedFrame);
                EXPECT_EQ(Runtime().lastSoundId, sound);
                EXPECT_EQ(m_random.State(), expected.State());
                EXPECT_TRUE(std::ranges::all_of(effectPool,
                                                [](const Client::World::ModelEffect& effect) { return effect.alive; }));
            }
        }
    }
}

// 0x4f589d..0x4f58f6 y 0x4f5938..0x4f5991: con lugar, el jugador de subtipo 0xd1..0xd3 y el 0x170 sueltan un hueso
// 0xd5/0 y diez 0xd6/0 sin dueño (150 y 100 más arriba, 0x51b398), cada uno con sus cinco rand() (0x51b72b), y siguen
// con el sonido 0x7c. El 0x16b sigue cortando en sus 0xe5 (CL11, lote del switch).
TEST_F(CombatRepliesTest, ThePlayerAndThe170ScatterTheirBones)
{
    constexpr std::size_t Bones = 11;
    constexpr std::size_t BoneDraws = 5;
    for (const auto& [model, subtype] : {std::pair{std::uint16_t{0x20a}, 0xd2}, std::pair{std::uint16_t{0x170}, 0}})
    {
        SCOPED_TRACE(model);
        Runtime().map = 0;
        Runtime().lastSoundId.reset();
        auto effectPool = m_world->Map().effects.Effects();
        for (Client::World::ModelEffect& effect : effectPool)
            effect.alive = false;
        ActorState& dead = Actor(20);
        dead = ActorState{};
        dead.alive = true;
        dead.type = model;
        dead.subtype = subtype;
        dead.animation.action = 7;
        dead.animation.frame = 1.0f;
        dead.position = {1000.0f, 2000.0f, 30.0f};
        CrtRandom expected = m_random;
        expected.Discard(Bones * BoneDraws);

        const auto died = Client::Scenes::SetActorDie(dead, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_FALSE(dead.alive);
        EXPECT_EQ(dead.animation.action, 7);
        EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x7c));
        EXPECT_EQ(m_random.State(), expected.State());
        for (std::size_t slot = 0; slot < Bones; ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::World::ModelEffect& bone = effectPool[slot];
            EXPECT_TRUE(bone.alive);
            EXPECT_EQ(bone.type, slot == 0 ? Client::World::FirstBoneEffect : Client::World::SecondBoneEffect);
            EXPECT_EQ(bone.subtype, 0);
            EXPECT_EQ(bone.ownerContext, nullptr);
            EXPECT_EQ(bone.transform.position.z, slot == 0 ? 180.0f : 130.0f);
            EXPECT_GE(bone.life, 32);
            EXPECT_LE(bone.life, 47);
        }
        EXPECT_FALSE(effectPool[Bones].alive);
    }
}

// El cierre del registro propio ocurre antes del rand() del sonido de Chaos Castle, y solo lo cierra el acceso que lo
// deja en 0. Incluso si la primera alta de efectos corta, el héroe ya quedó vivo=false y su vida en 0.
TEST_F(CombatRepliesTest, AHeroDeathClosesItsOwnRecordBeforeTheChaosSound)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0xff}})
    {
        SCOPED_TRACE(depth);
        Runtime().map = 18;
        Runtime().ownRecordDepth = depth;
        Runtime().lastSoundId.reset();
        m_world->OwnCharacter().life = 50;
        for (Client::World::ModelEffect& effect : m_world->Map().effects.Effects())
            effect.alive = true;

        ActorState& hero = Hero();
        hero = ActorState{};
        hero.alive = true;
        hero.type = 0x16b;
        hero.animation.action = 7;
        hero.animation.frame = 0.0f;
        CrtRandom expected = m_random;
        if (depth == 0)
            expected.Discard(OwnRecordDraws);
        const std::int32_t sound = static_cast<std::int32_t>((expected.Next() & 1) + 0x5e);

        const auto died = Client::Scenes::SetActorDie(hero, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_FALSE(hero.alive);
        EXPECT_EQ(hero.animation.action, 7);
        EXPECT_EQ(hero.animation.frame, DeathFrame);
        EXPECT_EQ(m_world->OwnCharacter().life, 0);
        EXPECT_EQ(Runtime().ownRecordDepth, depth);
        EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(sound));
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// 0x4f59ad..0x4f59f4: con lugar, la muerte del 0x16b (el héroe) deja ocho pares 0xe5/0 y 0xe6/0 en su posición, con
// sus ángulos y su luz, después del cierre del registro propio; cada uno saca sus ocho rand() (0x51b659), repetidos en
// un pool gemelo con el mismo CrtRandom. Con dos lugares libres, sólo el primer par; después, el sonido y el frame.
TEST_F(CombatRepliesTest, AHeroSpecialEffectDeathScattersItsSplinters)
{
    for (const std::size_t free : {std::size_t{2}, std::size_t{200}})
    {
        SCOPED_TRACE(free);
        Runtime().map = 0;
        Runtime().ownRecordDepth = 0;
        Runtime().lastSoundId.reset();
        m_world->OwnCharacter().life = 50;
        auto effectPool = m_world->Map().effects.Effects();
        for (std::size_t slot = 0; slot < effectPool.size(); ++slot)
            effectPool[slot].alive = slot >= free;

        ActorState& hero = Hero();
        hero = ActorState{};
        hero.alive = true;
        hero.type = 0x16b;
        hero.position = glm::vec3(1000.0f, 2000.0f, 300.0f);
        hero.angles = glm::vec3(0.0f, 0.0f, 45.0f);
        hero.light = glm::vec3(0.5f, 0.25f, 1.0f);
        hero.animation.action = 7;
        hero.animation.frame = 0.0f;
        CrtRandom expected = m_random;
        expected.Discard(OwnRecordDraws);
        CrtRandom counted = expected;

        const auto died = Client::Scenes::SetActorDie(hero, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        auto twin = std::make_unique<Client::World::ModelEffects>();
        for (std::size_t slot = free; slot < twin->Effects().size(); ++slot)
            twin->Effects()[slot].alive = true;
        for (int pair = 0; pair < 8; ++pair)
        {
            for (const std::int16_t type : {Client::World::FirstSplinterEffect, Client::World::SecondSplinterEffect})
                static_cast<void>(twin->CreateStoneChip(type, hero.position, hero.angles, hero.light, expected));
        }
        for (std::size_t slot = 0; slot < std::min<std::size_t>(free, 16); ++slot)
        {
            SCOPED_TRACE(slot);
            const Client::World::ModelEffect& made = effectPool[slot];
            const Client::World::ModelEffect& twinEffect = twin->Effects()[slot];
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, slot % 2 == 0 ? 0xe5 : 0xe6);
            EXPECT_EQ(made.subtype, 0);
            EXPECT_EQ(made.transform.position, twinEffect.transform.position);
            EXPECT_EQ(made.transform.angles, twinEffect.transform.angles);
            EXPECT_EQ(made.velocity, twinEffect.velocity);
            EXPECT_EQ(made.life, twinEffect.life);
            EXPECT_EQ(made.light, twinEffect.light);
        }
        if (free > 16)
        {
            EXPECT_FALSE(effectPool[16].alive);
        }
        EXPECT_FALSE(hero.alive);
        EXPECT_EQ(hero.animation.action, 7);
        EXPECT_EQ(hero.animation.frame, DeathFrame);
        EXPECT_EQ(m_world->OwnCharacter().life, 0);
        EXPECT_EQ(Runtime().ownRecordDepth, 0);
        EXPECT_EQ(m_random.State(), expected.State());
        int draws = 0;
        while (counted.State() != m_random.State() && draws < 1000)
        {
            static_cast<void>(counted.Next());
            ++draws;
        }
        EXPECT_EQ(draws, free > 16 ? 128 : 16);
    }
}

// El 0x9C que manda el GameServer 0.97 (0x49af90) lleva la clave 0xFFFF: 0x7fff con el bit 15. Nadie tiene la 0x7fff y
// 0x5110d0 devuelve el lugar 400, el de tipo 0, que muere con el sonido 0xd2 (nadie escribe la tabla del modelo 0 y el
// REP STOSD de 0x60dc80 la dejó en diez WORD en 0) y suma la experiencia (Q-C1160, P-C252).
TEST_F(CombatRepliesTest, TheBloodCastleRewardLandsOnSlotFourHundred)
{
    const auto applied = Client::Scenes::ApplyKillExperience({.key = 0x7fff, .shock = true, .amount = 100, .damage = 0},
                                                             Combat(), Chat());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(400).animation.action, 6);
    EXPECT_EQ(Actor(400).dying, 1);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0xd2));
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
        dead.floatCC = 9.0f;
        ActorState& killer = PlayerAt(20, 0x55);
        killer.position = {1300.0f, 2400.0f, 0.0f};
        ASSERT_TRUE(Client::Scenes::ApplyDeath({.key = 0x123, .skill = skill, .killer = 0x55}, Combat()).HasValue());

        CrtRandom expected = DrawnAfter(0);
        const std::int32_t first = expected.Next() % 5;
        const std::int32_t second = expected.Next() % 15;
        EXPECT_EQ(dead.byte1a1, 1);
        EXPECT_EQ(dead.byte398, 0);
        EXPECT_EQ(dead.floatCC, 0.0f);
        EXPECT_EQ(dead.floatD8, static_cast<float>((first + 10.0) * static_cast<double>(0.1f)));
        EXPECT_EQ(dead.floatD4, 0.0f);
        EXPECT_EQ(dead.floatD0, static_cast<float>(second + 40.0));
        EXPECT_EQ(dead.pushOrigin, dead.position);
        const auto angle = static_cast<float>(Client::Math::CreateAngle(1000.0f, 2000.0f, 1300.0f, 2400.0f));
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
    EXPECT_EQ(dead.floatCC, static_cast<float>(cc + 8.0));
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

// El puente de Blood Castle en el borde de la grilla: en la celda 0xffff la vecina de la derecha (0x4d0949) y en la 0
// sin el bit 8 en la 1 la de la izquierda (0x4d0979) caen fuera de TerrainWall. MuRet saca los tres rand() de antes de
// esa lectura y descarta el empujón (+0x1a1 en 0) sin cortar el handler (Q-C1180); en la celda 0 con el bit 8 en la 1,
// el empujón sale con sus cinco rand(). En Lorencia y en Devias la misma celda no empuja.
TEST_F(CombatRepliesTest, OnTheEdgeOfTheGridTheBridgePushIsDiscarded)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        glm::vec3 position;
        std::uint8_t x;
        std::uint8_t y;
        bool rightFalls;
        int draws;
        std::uint8_t pushed;
    };
    const std::array cases{Case{"la celda 0xffff", 11, {25550.0f, 25550.0f, 0.0f}, 255, 255, false, 3, 0},
                           Case{"la celda 0", 11, {50.0f, 50.0f, 0.0f}, 0, 0, false, 3, 0},
                           Case{"la celda 0 con la 1 que cae", 11, {50.0f, 50.0f, 0.0f}, 0, 0, true, 5, 1},
                           Case{"la celda 0 en Lorencia", 0, {50.0f, 50.0f, 0.0f}, 0, 0, false, 0, 0},
                           Case{"la celda 0xffff en Devias", 2, {25550.0f, 25550.0f, 0.0f}, 255, 255, false, 0, 0}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        m_random = CrtRandom(Seed);
        Runtime().map = probe.map;
        UseTerrain();
        Actor(12) = ActorState{};
        ActorState& dead = MonsterAt(12, 0x123);
        dead.position = probe.position;
        Wall(probe.x, probe.y) = 0x20;
        Wall(1, 0) = probe.rightFalls ? std::uint16_t{0x08} : std::uint16_t{0};

        const auto died = Client::Scenes::ApplyDeath({.key = 0x123, .skill = 0, .killer = 0}, Combat());

        ASSERT_TRUE(died.HasValue()) << died.Error();
        EXPECT_EQ(dead.dying, 1);
        EXPECT_EQ(dead.byte1a1, probe.pushed);
        EXPECT_EQ(m_random.State(), After(static_cast<std::uint32_t>(probe.draws)));
        if (probe.pushed != 0)
        {
            CrtRandom expected = DrawnAfter(0);
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            const std::int32_t push = expected.Next() % 10;
            EXPECT_EQ(dead.push, glm::vec3(0.0f, 0.0f, -static_cast<float>(push + 85.0)));
        }
    }
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
// rand() % 2, más 0xd2 (y 5 con el subtipo 9); con la WORD 2 en −1, ni sonido ni rand(); sin la tabla, las WORD en 0
// de 0x60dc80: el rand() y el 0xd2.
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
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0xd2));
    EXPECT_EQ(m_random.State(), After(1));

    Client::Sound::ModelSounds sounds{};
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

// 0x4ea030 hace no-op si no encuentra el modelo; 0x4f50b2 y 0x4f5265 siguen contando, y el sonido solo sortea con
// el frame en 0 o unordered (0x4f50b8..0x4f51ee).
TEST_F(CombatRepliesTest, AnAttackWithoutAModelKeepsItsAnimationButStillCountsAndSounds)
{
    const std::array<float, 3> frames{0.0f, 1.5f, UnorderedFrame};
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        SCOPED_TRACE(index);
        ActorState& monster = MonsterAt(12 + index, static_cast<std::uint16_t>(0x130 + index));
        monster.type = 0x15b;
        monster.animation.action = 7;
        monster.animation.frame = frames[index];
        const std::uint32_t frameBits = std::bit_cast<std::uint32_t>(frames[index]);
        Runtime().lastSoundId.reset();
        CrtRandom expected = m_random;
        const bool frameIsZeroOrUnordered = index != 1;
        if (frameIsZeroOrUnordered)
            static_cast<void>(expected.Next());

        const auto attacked = Client::Scenes::SetActorAttack(monster, Combat());
        ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
        EXPECT_EQ(monster.animation.action, 7);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(monster.animation.frame), frameBits);
        EXPECT_EQ(monster.attackCount, 2);
        EXPECT_EQ(Runtime().lastSoundId.has_value(), frameIsZeroOrUnordered);
        if (frameIsZeroOrUnordered)
        {
            EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0xd2));
        }
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// Un jugador con el subtipo 0xd1 a 0xd3 pide rand() & 1 + 0x3c y después el de la tabla del modelo 0x20a, que nadie
// escribe: sus WORD quedan en 0 (0x60dc80), así que el segundo rand() elige entre dos 0 y el último pedido es el 0xd2.
TEST_F(CombatRepliesTest, APlayerWithAModelSoundSubtypeDrawsTwice)
{
    ActorState& player = PlayerAt(20, 0x55);
    player.subtype = 0xd2;
    CrtRandom expected = DrawnAfter(OwnRecordDraws);
    const std::int32_t sound = (expected.Next() & 1) + 0x3c;
    static_cast<void>(expected.Next());
    ASSERT_TRUE(Client::Scenes::SetActorAttack(player, Combat()).HasValue());
    EXPECT_EQ(player.animation.action, 0x22);
    EXPECT_NE(sound, 0xd2);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0xd2));
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x43a2f0, en cualquier mapa: los de Kalima que leen +0x38e cierran su entrada (un rand()) antes de elegir; los del
// último grupo sacan rand() & 1. Ninguno suma +0x38f. Con la acción nueva el frame queda en 0 y 0x4f4a10 saca el
// rand() del sonido de la tabla, que nadie escribió para este modelo (las WORD en 0 de 0x60dc80).
TEST_F(CombatRepliesTest, TheKalimaMonstersChooseByTheirSkillByte)
{
    struct Case
    {
        std::uint16_t kind;
        std::uint8_t skill;
        // 0: ninguna; el monstruo sigue con la acción que tenía, la 0 (Q-C1172).
        std::uint8_t action;
    };
    const std::array cases{Case{0x91, 0x37, 4}, Case{0x10d, 0, 3},    Case{0xc1, 0x11, 4}, Case{0x110, 2, 3},
                           Case{0x95, 1, 4},    Case{0x111, 0x11, 3}, Case{0xbb, 2, 0}};
    std::uint32_t draws = 0;
    std::size_t slot = 12;
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.kind);
        ActorState& kalima = MonsterAt(slot++, 0x100);
        kalima.word376 = tested.kind;
        kalima.byte38e = tested.skill;
        kalima.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Scenes::SetActorAttack(kalima, Combat()).HasValue());
        EXPECT_EQ(kalima.animation.action, tested.action);
        EXPECT_EQ(kalima.attackCount, 1);
        draws += tested.action != 0 ? 2 : 1;
        EXPECT_EQ(m_random.State(), After(draws));
    }

    CrtRandom expected = DrawnAfter(draws);
    ActorState& drawing = MonsterAt(slot, 0x100);
    drawing.word376 = 0x113;
    drawing.animation.frame = 1.0f;
    const std::uint8_t action = static_cast<std::uint8_t>((expected.Next() & 1) + 3);
    static_cast<void>(expected.Next());
    ASSERT_TRUE(Client::Scenes::SetActorAttack(drawing, Combat()).HasValue());
    EXPECT_EQ(drawing.animation.action, action);
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x443750: el 0x123 en el mapa 31 elige con +0x38e abierto ('2' la 3, otro la 4) y cierra después. Fuera del 31,
// la 3 o la 4 de siempre, por +0x38f.
TEST_F(CombatRepliesTest, TheTrialsMonsterChoosesWithItsSkillByteOpen)
{
    Runtime().map = 0x1f;
    for (const std::uint8_t skill : {std::uint8_t{0x32}, std::uint8_t{0x11}})
    {
        SCOPED_TRACE(skill);
        ActorState& trials = MonsterAt(12, 0x100);
        trials.word376 = 0x123;
        trials.byte38e = skill;
        trials.attackCount = 0;
        trials.animation.action = skill == 0x32 ? 3 : 4;
        trials.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Scenes::SetActorAttack(trials, Combat()).HasValue());
        EXPECT_EQ(trials.animation.action, skill == 0x32 ? 3 : 4);
        EXPECT_EQ(trials.animation.frame, 1.0f);
        EXPECT_EQ(trials.attackCount, 1);
    }
    EXPECT_EQ(m_random.State(), After(2));

    Runtime().map = 0;
    ActorState& elsewhere = MonsterAt(13, 0x101);
    elsewhere.word376 = 0x123;
    elsewhere.animation.action = 3;
    elsewhere.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(elsewhere, Combat()).HasValue());
    EXPECT_EQ(elsewhere.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(2));
}

// 0x429270 y 0x437580 con 0x4f92f0: +0x42e (sin signo) buscado entre las cinco skills de MonsterSkill.bmd del +0x376;
// en el lugar 0 a 4, las acciones 3, 4, 8, 9 y 0xb y el ayudante atiende. Encuentre o no, +0x42e queda en 0xff y su
// entrada se cierra (un rand()). Sin encontrarla, la 3 o la 4 por +0x38f.
TEST_F(CombatRepliesTest, TheSkillMonstersLookTheirSkillUpInTheTable)
{
    Client::Tables::MonsterSkills crywolf;
    crywolf.skills = {6, 7, 8, 0xc8, -1};
    Runtime().monsterSkills.records[0x154] = crywolf;
    Runtime().map = 0x22;
    struct Case
    {
        std::uint8_t skill;
        std::uint8_t action;
    };
    std::size_t slot = 12;
    for (const Case tested : {Case{6, 3}, Case{7, 4}, Case{8, 8}, Case{0xc8, 9}})
    {
        SCOPED_TRACE(tested.skill);
        ActorState& monster = MonsterAt(slot++, 0x100);
        monster.word376 = 0x154;
        monster.byte42e = tested.skill;
        monster.animation.action = tested.action;
        monster.animation.frame = 1.0f;
        ASSERT_TRUE(Client::Scenes::SetActorAttack(monster, Combat()).HasValue());
        EXPECT_EQ(monster.animation.action, tested.action);
        EXPECT_EQ(monster.byte42e, 0xff);
        EXPECT_EQ(monster.attackCount, 1);
    }
    EXPECT_EQ(m_random.State(), After(4));

    // 0xff no es −1: no la encuentra, y el que vuelve a pegar ya la tiene en 0xff.
    ActorState& again = MonsterAt(slot++, 0x100);
    again.word376 = 0x154;
    again.byte42e = 0xff;
    again.animation.action = 3;
    again.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(again, Combat()).HasValue());
    EXPECT_EQ(again.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(5));

    // Los mapas 0x21 y 0x36 con 0x130, 0x131 y 0x135 (0x429270); sin registro, la .bss en 0 encuentra el 0.
    Runtime().map = 0x36;
    ActorState& doppelganger = MonsterAt(slot++, 0x100);
    doppelganger.word376 = 0x131;
    doppelganger.animation.action = 3;
    doppelganger.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(doppelganger, Combat()).HasValue());
    EXPECT_EQ(doppelganger.attackCount, 1);
    EXPECT_EQ(doppelganger.byte42e, 0xff);
    EXPECT_EQ(m_random.State(), After(6));

    // En otro mapa, ni lo mira.
    Runtime().map = 0;
    ActorState& elsewhere = MonsterAt(slot, 0x100);
    elsewhere.word376 = 0x154;
    elsewhere.byte42e = 6;
    elsewhere.animation.action = 3;
    elsewhere.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(elsewhere, Combat()).HasValue());
    EXPECT_EQ(elsewhere.byte42e, 6);
    EXPECT_EQ(elsewhere.attackCount, 2);
    EXPECT_EQ(m_random.State(), After(6));
}

// El 0x19e pone +0x110 en 3 y la acción 5 (0x42dcb0) sin contar.
TEST_F(CombatRepliesTest, TheSpecialMonsterSetsItsActionWithoutCountingTwice)
{
    ActorState& special = MonsterAt(15, 0x126);
    special.type = 0x19e;
    special.animation.action = 5;
    special.animation.frame = 1.0f;
    ASSERT_TRUE(Client::Scenes::SetActorAttack(special, Combat()).HasValue());
    EXPECT_EQ(special.byte110, 3);
    EXPECT_EQ(special.animation.action, 5);
    EXPECT_EQ(special.attackCount, 1);
}

// 0x4f4f5b y 0x4f4fb9: si el pool de efectos no tiene lugar, no se cambia la acción; igual se pide el sonido nativo,
// corre 0x4f50b8..0x4f5262 con su regla de frame/tabla y al final se suma +0x38f una sola vez.
TEST_F(CombatRepliesTest, EffectsWithoutPoolSpaceKeepTheAnimationAndFinishAttackBookkeeping)
{
    struct Case
    {
        float frame;
        bool frameIsZeroOrUnordered;
        std::int32_t subtype;
        std::optional<Client::Sound::ModelSounds> sounds;
    };
    Client::Sound::ModelSounds silent{};
    silent.fill(-1);
    Client::Sound::ModelSounds zeroed{};
    zeroed.fill(0);
    const std::array<Case, 6> cases{{
        {1.0f, false, 0xd2, zeroed},
        {0.0f, true, 0, silent},
        {0.0f, true, 0xd1, zeroed},
        {0.0f, true, 0xd3, silent},
        {0.0f, true, 9, zeroed},
        {UnorderedFrame, true, 0, std::nullopt},
    }};

    constexpr std::array<std::uint16_t, 2> EffectModels{0x27, 0x33};
    std::size_t slot = 12;
    for (const std::uint16_t model : EffectModels)
    {
        for (const Case& test : cases)
        {
            SCOPED_TRACE(model);
            SCOPED_TRACE(test.subtype);
            SCOPED_TRACE(std::bit_cast<std::uint32_t>(test.frame));
            auto effectPool = m_world->Map().effects.Effects();
            for (Client::World::ModelEffect& poolEffect : effectPool)
                poolEffect.alive = true;
            const std::uint16_t key = static_cast<std::uint16_t>(0x200 + slot);
            ActorState& effect = MonsterAt(slot, key);
            ++slot;
            effect.type = model;
            effect.subtype = test.subtype;
            effect.attackCount = 0xff;
            effect.animation.action = 7;
            effect.animation.frame = test.frame;
            const std::uint32_t frameBits = std::bit_cast<std::uint32_t>(test.frame);
            Runtime().lastSoundId.reset();

            ActorCombatContext combat = Combat();
            if (test.sounds)
            {
                const Client::Sound::ModelSounds sounds = *test.sounds;
                combat.modelSounds = [model,
                                      sounds](std::uint16_t requested) -> std::optional<Client::Sound::ModelSounds>
                {
                    if (requested != model)
                        return std::nullopt;
                    return sounds;
                };
            }

            CrtRandom expected = m_random;
            std::optional<std::int32_t> expectedSound = model == 0x27 ? 0x10 : 0x79;
            if (test.frameIsZeroOrUnordered)
            {
                if (test.subtype >= 0xd1 && test.subtype <= 0xd3)
                    expectedSound = static_cast<std::int32_t>((expected.Next() & 1) + 0x3c);
                const Client::Sound::ModelSounds sounds = test.sounds.value_or(Client::Sound::ModelSounds{});
                if (sounds[2] != -1)
                {
                    const auto drawn = static_cast<std::size_t>(expected.Next() % 2);
                    expectedSound = sounds[2 + drawn] + (test.subtype == 9 ? 5 : 0) + 0xd2;
                }
            }

            const auto attacked = Client::Scenes::SetActorAttack(effect, combat);
            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            EXPECT_EQ(effect.animation.action, 7);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.animation.frame), frameBits);
            EXPECT_EQ(effect.attackCount, 0);
            EXPECT_EQ(Runtime().lastSoundId, expectedSound);
            EXPECT_EQ(m_random.State(), expected.State());
            EXPECT_TRUE(std::ranges::all_of(effectPool, [](const Client::World::ModelEffect& poolEffect)
                                            { return poolEffect.alive; }));
        }
    }
}

// 0x4f4f3a..0x4f4f6a: con un lugar libre, el 0x27 crea la sierra 0xd4/0 sin dueño en su posición (130 más arriba,
// 0x51a4ee), con sus ángulos y vida 10, sin rand(), y sigue con el sonido 0x10 y +0x38f.
TEST_F(CombatRepliesTest, TheSawOf27GoesOutWithTheStrike)
{
    ActorState& saw = MonsterAt(12, 0x20c);
    saw.type = 0x27;
    saw.attackCount = 0xff;
    saw.animation.action = 7;
    saw.animation.frame = 1.0f;
    saw.position = {300.0f, 400.0f, 50.0f};
    saw.angles = {0.0f, 0.0f, 45.0f};
    Runtime().lastSoundId.reset();
    auto effectPool = m_world->Map().effects.Effects();
    for (Client::World::ModelEffect& poolEffect : effectPool)
        poolEffect.alive = true;
    effectPool[3].alive = false;
    const CrtRandom expected = m_random;

    ASSERT_TRUE(Client::Scenes::SetActorAttack(saw, Combat()).HasValue());

    const Client::World::ModelEffect& created = effectPool[3];
    EXPECT_TRUE(created.alive);
    EXPECT_EQ(created.type, Client::World::SawEffect);
    EXPECT_EQ(created.subtype, 0);
    EXPECT_EQ(created.ownerContext, nullptr);
    EXPECT_EQ(created.life, 10);
    EXPECT_EQ(created.transform.position, glm::vec3(300.0f, 400.0f, 180.0f));
    EXPECT_EQ(created.transform.angles, saw.angles);
    EXPECT_EQ(saw.animation.action, 7);
    EXPECT_EQ(saw.attackCount, 0);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x10));
    EXPECT_EQ(m_random.State(), expected.State());
}

// 0x4f4f98..0x4f4fc4: con un lugar libre, el 0x33 crea el golpe 0x5da/0 sin dueño (Scenes/SkillBurstEffects): vida 10
// y (0, −60, 0) girado por sus ángulos (en 0) más su posición, con 130 más de z, sin rand(); sigue con el sonido 0x79 y
// +0x38f.
TEST_F(CombatRepliesTest, TheFlameOf33GoesOutWithTheStrike)
{
    ActorState& burner = MonsterAt(12, 0x20c);
    burner.type = 0x33;
    burner.attackCount = 0xff;
    burner.animation.action = 7;
    burner.animation.frame = 1.0f;
    burner.position = {300.0f, 400.0f, 50.0f};
    burner.angles = glm::vec3(0.0f);
    burner.light = {0.5f, 0.25f, 1.0f};
    Runtime().lastSoundId.reset();
    auto effectPool = m_world->Map().effects.Effects();
    for (Client::World::ModelEffect& poolEffect : effectPool)
        poolEffect.alive = true;
    effectPool[3].alive = false;
    const CrtRandom expected = m_random;

    const auto attacked = Client::Scenes::SetActorAttack(burner, Combat());

    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    const Client::World::ModelEffect& created = effectPool[3];
    EXPECT_TRUE(created.alive);
    EXPECT_EQ(created.type, 0x5da);
    EXPECT_EQ(created.subtype, 0);
    EXPECT_EQ(created.ownerContext, nullptr);
    EXPECT_EQ(created.life, 10);
    EXPECT_EQ(created.transform.position, glm::vec3(300.0f, 340.0f, 180.0f));
    EXPECT_EQ(created.light, burner.light);
    EXPECT_EQ(burner.animation.action, 7);
    EXPECT_EQ(burner.attackCount, 0);
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x79));
    EXPECT_EQ(m_random.State(), expected.State());
}

// La tabla de sonidos que escribió el alta (WorldRuntime::modelSounds) es la de 0x4f4a10 si el contexto no trae otra.
TEST_F(CombatRepliesTest, TheModelSoundsComeFromTheRuntimeTable)
{
    Client::Sound::ModelSounds sounds{};
    sounds.fill(-1);
    sounds[2] = 0x14;
    sounds[3] = 0x15;
    Runtime().modelSounds[Monster] = sounds;
    CrtRandom expected = DrawnAfter(0);
    const std::int32_t drawn = expected.Next() % 2;
    ActorState& monster = MonsterAt(12, 0x100);
    ASSERT_TRUE(Client::Scenes::SetActorAttack(monster, Combat()).HasValue());
    EXPECT_EQ(Runtime().lastSoundId, std::optional<std::int32_t>(0x14 + drawn + 0xd2));
    EXPECT_EQ(m_random.State(), expected.State());
}
} // namespace
