#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/PlayerKey.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Actors/ActionReply.h"
#include "Combat/ActorCombat.h"
#include "Actors/ActorDying.h"
#include "Actors/ActorShock.h"
#include "Kernel/ActorState.h"
#include "Combat/AttackStep.h"
#include "Actors/CharacterStep.h"
#include "Chat/ChatReceive.h"
#include "Combat/CombatReplies.h"
#include "Sound/MonsterSounds.h"
#include "Monsters/MonsterViewport.h"
#include "Actors/MovementReplies.h"
#include "Scenes/SceneId.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Particles/Particles.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/Packets.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los monstruos del 0x13 en combate (CL9 hito 3, fase 2c): el alta escribe la tabla de sonidos de su modelo (0x612d90),
// el 0x18 con la 0x78 los hace pegar (0x4f4a10 con sus ayudantes) y contar en el paso (0x4fd5f0 con 0x4f97b0 y
// 0x4f5b70), y el D7, el 0x16 y el 0x17 los lastiman y los matan (0x4f5660 y 0x4fb9e0). Con el Data del laboratorio
// (Lorencia y Devias); las claves son inventadas y el alta carga el modelo del monstruo (0x612d90, CL9 hito 2) con una
// tabla de texturas falsa.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Sound::ModelSounds;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::MonsterRecordBytes;

class CombatMessageMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::int32_t HeroSlot = 5;
constexpr std::uint16_t MonsterKey = 0x0123;
// El tipo 3 de Lorencia: Monster10.bmd (0x612d90(9)) en el modelo 0x15b.
constexpr std::uint8_t LorenciaType = 3;
constexpr std::uint16_t LorenciaModel = 0x15b;

int DrawsSince(std::uint32_t state, const CrtRandom& random)
{
    constexpr int Limit = 100'000;
    CrtRandom probe(state);
    int draws = 0;
    while (probe.State() != random.State() && draws < Limit)
    {
        static_cast<void>(probe.Next());
        ++draws;
    }
    return draws;
}

// La biblioteca del mapa, detrás de AssetsState::fauna: Player.bmd, también en el modelo del monstruo de Lorencia.
class MonsterModels final : public Client::Models::IModelLibrary
{
public:
    [[nodiscard]] const Client::Data::Models::Model* Find(Client::Models::ModelId /*model*/) const override
    {
        return nullptr;
    }
    [[nodiscard]] const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == PlayerSkeletonModel ? &skeleton : nullptr;
    }
    [[nodiscard]] std::span<const Client::Render::TextureSlot>
    MeshTextures(Client::Models::ModelId /*model*/) const override
    {
        return {};
    }

    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

// El mundo del juego, en el heap: WorldState pesa unos 350 KB.
struct Game
{
    Game()
    {
        // 0x612d90 sube las texturas de los modelos que carga por la tabla que liga el juego (CL9 hito 2).
        states.assets.fauna.TextureTable().Bind(store);
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        world.Runtime().hero = 0;
        world.Runtime().heroSlot = HeroSlot;
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = dataRoot,
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log,
                .states = &states};
    }
    Client::Actors::CharacterStepContext Step()
    {
        return {.world = world,
                .random = cloth.random,
                .playerSkeleton = &models.skeleton,
                .models = &models,
                .terrain = &states.terrain.current};
    }
    // Antes que el mundo: FaunaModels suelta sus texturas al destruirse.
    Client::Tests::FakeTextureStore store;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    std::filesystem::path dataRoot;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    MonsterModels models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class MonsterCombatTest : public Client::Tests::GoldenDataTest
{
protected:
    void Load(std::uint8_t map)
    {
        game = std::make_unique<Game>();
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->states.terrain.current = std::move(loaded).Value();
        game->world.Runtime().map = map;
        game->dataRoot = m_dataRoot;
    }

    // Da de alta un monstruo del tipo `type` en la primera celda libre desde (100, 100) y devuelve su lugar.
    std::size_t Create(std::uint8_t type)
    {
        const auto& cells = game->states.terrain.current.attributes->cells;
        std::uint8_t x = 100;
        std::uint8_t y = 100;
        while (cells[Client::Data::Terrain::CellIndex(x, y)] != 0)
            ++x;
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = MonsterKey, .typeHigh = 0x00, .typeLow = type, .x = x, .y = y, .targetX = x, .targetY = y}}};
        auto read = Client::Protocol::Actors::ReadMonsters(Client::Tests::MonstersPacket(records));
        EXPECT_TRUE(read.HasValue());
        const auto applied = Client::Monsters::ApplyViewportMonsters(read.Value(), game->Resources());
        EXPECT_TRUE(applied.HasValue()) << (applied.HasValue() ? "" : applied.Error());
        for (std::size_t slot = 0; slot < game->world.Actors().size(); ++slot)
        {
            if (game->world.Actors()[slot].alive && game->world.Actors()[slot].key == MonsterKey)
                return slot;
        }
        ADD_FAILURE() << "el monstruo no está en el pool";
        return 0;
    }

    std::unique_ptr<Game> game;
};

ModelSounds Sounds(std::int16_t first, std::int16_t second, std::int16_t third, std::int16_t fourth, std::int16_t fifth)
{
    return {first, second, third, fourth, fifth, -1, -1, -1, -1, -1};
}

// El alta (0x612d90 desde 0x5141e0) escribe la tabla de sonidos del modelo: el 3 de Lorencia la del caso 9 (0x15b) y
// el 19 de Devias, el caso 0xc, las de los dos modelos del yeti (0x15e y 0x15f).
TEST_F(MonsterCombatTest, TheCreationWritesTheModelSounds)
{
    Load(0);
    static_cast<void>(Create(LorenciaType));
    const auto& lorencia = game->world.Runtime().modelSounds;
    ASSERT_EQ(lorencia.size(), 1u);
    EXPECT_EQ(lorencia.at(LorenciaModel), Sounds(0xd, 0xd, 0xd, 0xd, 0xd));

    Load(2);
    static_cast<void>(Create(19));
    const auto& devias = game->world.Runtime().modelSounds;
    ASSERT_EQ(devias.size(), 2u);
    EXPECT_EQ(devias.at(0x15e), Sounds(0x44, 0x44, 0x46, 0x46, 0x47));
    EXPECT_EQ(devias.at(0x15f), Sounds(0x44, 0x45, 0x46, 0x46, 0x47));
}

// El 0x18 con la 0x78: 0x4f4a10 sin ayudantes (Lorencia) pone la 3, cuenta +0x38f y pide el sonido de pegar de su
// tabla (rand() % 2 entre la WORD 2 y la 3, más 0xd2); después +0x381 en 1. El paso del ataque cuenta como el de un
// jugador y, en el frame 5, corre la habilidad sobre el héroe, con el sonido de las manos del registro propio.
TEST_F(MonsterCombatTest, AMonsterAttacksAndStepsItsAttack)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];
    ASSERT_NE(game->states.assets.fauna.SkeletonOf(Client::Models::ModelId{LorenciaModel}), nullptr);
    EXPECT_EQ(game->models.SkeletonOf(Client::Models::ModelId{LorenciaModel}), nullptr);
    const std::uint32_t created = game->cloth.random.State();

    const Client::Protocol::Movement::Action attack{.key = MonsterKey, .direction = 0, .action = 0x78};
    const auto attacked = Client::Actors::ApplyActionReply(attack, game->Resources(), {});
    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    EXPECT_EQ(monster.animation.action, 3);
    EXPECT_EQ(monster.attackCount, 2);
    EXPECT_EQ(monster.byte381, 1);
    EXPECT_EQ(monster.attackTarget, static_cast<std::uint16_t>(HeroSlot));
    EXPECT_EQ(game->world.Runtime().lastAttacker, static_cast<std::int32_t>(slot));
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0xd + 0xd2});
    EXPECT_EQ(DrawsSince(created, game->cloth.random), 1);

    monster.attackTarget = 0;
    const std::uint32_t before = game->cloth.random.State();
    std::vector<std::int32_t> sounds;
    Client::Actors::CharacterStepContext context = game->Step();
    context.soundRequest = [&sounds](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); };
    for (int step = 1; step < 4; ++step)
    {
        const auto stepped = Client::Combat::StepTheAttack(monster, slot, context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, step + 1);
    }
    // Cada paso: la lectura de 0x4f97b0, el rand() de 0x4f5b97 y las dos lecturas de 0x4f811b y 0x4f8a2c.
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4);

    monster.animation.frame = 5.0f;
    const auto struck = Client::Combat::StepTheAttack(monster, slot, context);
    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(monster.byte381, 0);
    // El paso con el registro propio (4 + 3.848) y la habilidad con blanco (3.853).
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4 + 4 + 0xf08 + 0xf08 + 5);
    EXPECT_EQ(sounds.size(), 1u);
}

// El D7, el 0x16 con el bit 15 y el 0x17 sobre el monstruo: el daño con el golpe (0x4f53a0, el sonido de su tabla),
// 0x4f5660 (la acción de morir y el sonido de la WORD 4) y el que se muere, que 0x4fb9e0 lleva hasta borrarlo.
TEST_F(MonsterCombatTest, AMonsterIsHurtKilledAndDies)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];

    const Client::Protocol::Movement::Damage hurt{
        .characterId = MonsterKey, .shock = false, .amount = 12, .type = 0, .flag = false};
    const std::uint32_t created = game->cloth.random.State();
    const auto damaged = Client::Actors::ApplyDamageReply(hurt, Client::Kernel::PlayerKey{HeroKey}, game->Resources());
    ASSERT_TRUE(damaged.HasValue()) << damaged.Error();
    // 0x4f53a0: la acción 5 y, con el frame en 0, el golpe de la WORD 2 o la 3 de su tabla (0x4f553d..0x4f55ac).
    EXPECT_EQ(monster.animation.action, 5);
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0xd + 0xd2});
    EXPECT_GE(DrawsSince(created, game->cloth.random), 1);
    game->world.Runtime().lastSoundId.reset();

    Client::Chat::ChatReceiveContext chat{.world = game->world, .random = game->cloth.random};
    const Client::Protocol::Combat::KillExperience kill{.key = MonsterKey, .shock = true, .amount = 0, .damage = 12};
    const auto killed =
        Client::Combat::ApplyKillExperience(kill, Client::Combat::CombatContextOf(game->Resources()), chat);
    ASSERT_TRUE(killed.HasValue()) << killed.Error();
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0xd + 0xd2});

    const Client::Protocol::Combat::Death death{.key = MonsterKey, .skill = 0, .killer = HeroKey};
    const auto died = Client::Combat::ApplyDeath(death, Client::Combat::CombatContextOf(game->Resources()));
    ASSERT_TRUE(died.HasValue()) << died.Error();
    EXPECT_NE(monster.dying, 0);

    int steps = 0;
    while (monster.alive && steps < 200)
    {
        const auto stepped = Client::Actors::StepActorDying(
            monster, slot, {.map = 0, .scene = Client::Scenes::SceneId::Game, .random = game->cloth.random});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ++steps;
    }
    EXPECT_FALSE(monster.alive);
    EXPECT_GT(steps, 50);
}

// Tras un golpe normal, la captura entrega primero el 0x17 de cinco bytes y luego el 0x16 sin shock. Los lectores
// reciben los payloads en claro después del descifrado de red; ambas respuestas se aplican al monstruo dado de alta
// desde el Data real del fixture.
TEST_F(MonsterCombatTest, APlainDeathThenKillExperienceReplyFinishesTheMonster)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];
    monster.position = {1000.0f, 2000.0f, 30.0f};
    monster.movement = 1;

    const auto hit = Client::Protocol::Movement::ReadDamage(
        Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 8, 0xd7, 0x01, 0x23, 0x00, 0x0c, 0x00})));
    ASSERT_TRUE(hit.HasValue());
    const auto appliedHit =
        Client::Actors::ApplyDamageReply(hit.Value(), Client::Kernel::PlayerKey{HeroKey}, game->Resources());
    ASSERT_TRUE(appliedHit.HasValue()) << appliedHit.Error();

    const auto death =
        Client::Protocol::Combat::ReadDeath(Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 5, 0x17, 0x01, 0x23})));
    ASSERT_TRUE(death.HasValue());
    EXPECT_EQ(death.Value().key, MonsterKey);
    EXPECT_EQ(death.Value().skill, 0);
    EXPECT_EQ(death.Value().killer, 0);

    const auto experience = Client::Protocol::Combat::ReadKillExperience(
        Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 9, 0x16, 0x01, 0x23, 0x36, 0x8c, 0x12, 0x16})));
    ASSERT_TRUE(experience.HasValue());
    EXPECT_EQ(experience.Value().key, MonsterKey);
    EXPECT_FALSE(experience.Value().shock);
    EXPECT_EQ(experience.Value().amount, 13964u);
    EXPECT_EQ(experience.Value().damage, 4630);

    const auto appliedDeath =
        Client::Combat::ApplyDeath(death.Value(), Client::Combat::CombatContextOf(game->Resources()));
    ASSERT_TRUE(appliedDeath.HasValue()) << appliedDeath.Error();
    EXPECT_EQ(monster.dying, 1);
    EXPECT_EQ(monster.movement, 0);
    EXPECT_EQ(monster.animation.action, 5);

    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{486, "Experiencia %d"}});
    CombatMessageMeasure measure;
    Client::Chat::ChatReceiveContext chat{
        .world = game->world, .random = game->cloth.random, .measure = &measure, .texts = &texts};
    const auto appliedExperience = Client::Combat::ApplyKillExperience(
        experience.Value(), Client::Combat::CombatContextOf(game->Resources()), chat);
    ASSERT_TRUE(appliedExperience.HasValue()) << appliedExperience.Error();

    EXPECT_EQ(monster.dying, 1);
    EXPECT_EQ(monster.movement, 0);
    const ActorState& hero = game->world.Actors()[0];
    EXPECT_EQ(hero.byte380, 2);
    EXPECT_EQ(hero.word382, 4630);
    EXPECT_EQ(hero.attackTarget, slot);
    const auto& damageNumbers = game->world.DrawTail().damage;
    const auto damage = std::find_if(damageNumbers.begin(), damageNumbers.end(),
                                     [](const auto& number) { return number.alive && number.number == 4630; });
    ASSERT_NE(damage, damageNumbers.end());
    EXPECT_EQ(damage->position, glm::vec3(1000.0f, 2000.0f, 170.0f));
    EXPECT_EQ(game->world.OwnCharacter().experience, 13964u);
    ASSERT_EQ(game->world.Runtime().chat.log.entries.size(), 1u);
    EXPECT_EQ(game->world.Runtime().chat.log.entries[0].text, "Experiencia 13964");

    int steps = 0;
    while (monster.alive && steps < 200)
    {
        const auto stepped = Client::Actors::StepActorDying(
            monster, slot,
            {.map = 0, .scene = Client::Scenes::SceneId::Game, .random = game->cloth.random, .world = &game->world});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ++steps;
    }
    EXPECT_FALSE(monster.alive);
    EXPECT_GT(steps, 50);
}

// 0x4f553d..0x4f55bb y 0x4f55c0..0x4f5649: la tabla del registro canónico manda el sonido del golpe; el tipo 0x18f
// conserva cinco oportunidades de polvo y termina siempre con el sonido 0x86, incluso si el pool está lleno.
TEST_F(MonsterCombatTest, TheBlackSkullKeepsItsSparkLoopAcrossFreePartialAndFullPools)
{
    struct PoolCase
    {
        const char* name;
        std::size_t initiallyFree;
        std::size_t expectedSparks;
    };
    for (const PoolCase probe :
         {PoolCase{"libre", Client::Particles::ParticleCount, 2}, PoolCase{"parcial", 1, 1}, PoolCase{"lleno", 0, 0}})
    {
        SCOPED_TRACE(probe.name);
        Load(0);
        const std::size_t slot = Create(LorenciaType);
        ActorState& monster = game->world.Actors()[slot];
        monster.type = 0x18f;
        monster.animation = {.action = 3, .frame = 0.0f};
        monster.position = {100.0f, 200.0f, 300.0f};
        monster.angles = {10.0f, 20.0f, 30.0f};
        monster.light = {0.25f, 0.5f, 0.75f};
        game->world.Runtime().modelSounds[0x18f] = Sounds(0x31, 0x32, 0x41, 0x42, 0x43);

        auto actual = game->world.Particles().All();
        auto expected = std::make_unique<Client::Particles::Particles>();
        if (probe.initiallyFree != Client::Particles::ParticleCount)
        {
            for (std::size_t index = 0; index < actual.size(); ++index)
            {
                actual[index].alive = true;
                actual[index].type = 0x1234;
                expected->All()[index] = actual[index];
            }
            if (probe.initiallyFree != 0)
            {
                actual.back().alive = false;
                expected->All().back().alive = false;
            }
        }

        constexpr std::uint32_t SparkSeed = 2;
        game->cloth.random = CrtRandom(SparkSeed);
        game->world.Runtime().lastSoundId.reset();
        CrtRandom expectedRandom(SparkSeed);
        static_cast<void>(expectedRandom.Next()); // El sonido de golpe precede al bucle de las chispas.
        std::size_t expectedSparkCount = 0;
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if ((expectedRandom.Next() & 1) != 0)
                continue;
            const float x = static_cast<float>(static_cast<double>(monster.position.x) +
                                               static_cast<double>(expectedRandom.Next() % 128 - 64));
            const float z = static_cast<float>(
                (static_cast<double>(monster.position.z) + static_cast<double>(expectedRandom.Next() % 50)) + 200.0);
            const auto created = expected->CreateChecked(Client::Particles::DustParticle, {x, monster.position.y, z},
                                                         monster.angles, monster.light, 0, 1.0f, 0.0f, expectedRandom);
            ASSERT_TRUE(created.HasValue());
            expectedSparkCount += created.Value().has_value() ? 1u : 0u;
        }

        const auto shocked = Client::Actors::SetActorShock(monster, 30, game->Resources());
        EXPECT_TRUE(shocked.HasValue()) << (shocked.HasValue() ? "" : shocked.Error());
        EXPECT_EQ(monster.animation.action, 3);
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x86});
        EXPECT_EQ(game->cloth.random.State(), expectedRandom.State());
        EXPECT_EQ(expectedSparkCount, probe.expectedSparks);

        const auto actualParticles = game->world.Particles().All();
        const auto expectedParticles = expected->All();
        std::size_t actualSparkCount = 0;
        for (std::size_t index = 0; index < actualParticles.size(); ++index)
        {
            const auto& actualParticle = actualParticles[index];
            const auto& expectedParticle = expectedParticles[index];
            EXPECT_EQ(actualParticle.alive, expectedParticle.alive) << index;
            EXPECT_EQ(actualParticle.type, expectedParticle.type) << index;
            if (!actualParticle.alive || actualParticle.type != Client::Particles::DustParticle)
                continue;
            ++actualSparkCount;
            EXPECT_EQ(actualParticle.subtype, 0) << index;
            EXPECT_EQ(actualParticle.position, expectedParticle.position) << index;
            EXPECT_EQ(actualParticle.angles, expectedParticle.angles) << index;
            EXPECT_EQ(actualParticle.light, monster.light) << index;
            EXPECT_EQ(actualParticle.life, expectedParticle.life) << index;
        }
        EXPECT_EQ(actualSparkCount, probe.expectedSparks);
    }
}

// 0x612d90 carga el mismo modelo y registro de sonidos que consume el ataque y la muerte. Una escritura posterior al
// alta en WorldRuntime.modelSounds es la tabla canónica para el golpe (0x4f553d..0x4f55a7).
TEST_F(MonsterCombatTest, AMonsterHitUsesTheCanonicalRuntimeSoundRecord)
{
    Load(0);
    const std::size_t slot = Create(LorenciaType);
    ActorState& monster = game->world.Actors()[slot];
    monster.animation = {.action = 3, .frame = 0.0f};
    const ModelSounds sounds = Sounds(0x31, 0x32, 0x41, 0x42, 0x43);
    game->world.Runtime().modelSounds[LorenciaModel] = sounds;

    constexpr std::uint32_t HitSeed = 0x2468;
    game->cloth.random = CrtRandom(HitSeed);
    CrtRandom expected(HitSeed);
    const std::size_t word = 2 + static_cast<std::size_t>(expected.Next() & 1);
    const auto shocked = Client::Actors::SetActorShock(monster, 30, game->Resources());
    ASSERT_TRUE(shocked.HasValue()) << shocked.Error();
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{sounds[word] + 0xd2});
    EXPECT_EQ(game->cloth.random.State(), expected.State());
}

// 0x4f5496: un frame distinto de cero salta el sonido; con NaN los dos saltos ordenados son falsos y consume su rand().
// En ambos casos, los cinco gates impares siguen sin crear polvo y el 0x18f termina con 0x86.
TEST_F(MonsterCombatTest, TheBlackSkullKeepsItsFinalSoundWhenEverySparkGateMisses)
{
    struct FrameCase
    {
        const char* name;
        float frame;
        std::uint32_t seed;
        bool soundDraw;
    };
    for (const FrameCase probe : {FrameCase{"frame no cero", 1.0f, 29, false},
                                  FrameCase{"frame unordered", std::numeric_limits<float>::quiet_NaN(), 63, true}})
    {
        SCOPED_TRACE(probe.name);
        Load(0);
        const std::size_t slot = Create(LorenciaType);
        ActorState& monster = game->world.Actors()[slot];
        monster.type = 0x18f;
        monster.animation = {.action = 3, .frame = probe.frame};
        game->world.Runtime().modelSounds[0x18f] = Sounds(0x31, 0x32, 0x41, 0x42, 0x43);
        game->cloth.random = CrtRandom(probe.seed);
        game->world.Runtime().lastSoundId.reset();

        CrtRandom expected(probe.seed);
        if (probe.soundDraw)
            static_cast<void>(expected.Next());
        for (int attempt = 0; attempt < 5; ++attempt)
            EXPECT_NE(expected.Next() & 1, 0);

        const auto shocked = Client::Actors::SetActorShock(monster, 30, game->Resources());
        ASSERT_TRUE(shocked.HasValue()) << shocked.Error();
        EXPECT_EQ(monster.animation.action, 3);
        if (std::isnan(probe.frame))
            EXPECT_TRUE(std::isnan(monster.animation.frame));
        else
            EXPECT_EQ(monster.animation.frame, probe.frame);
        EXPECT_FALSE(game->world.Particles().Any());
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x86});
        EXPECT_EQ(game->cloth.random.State(), expected.State());
    }
}

// En Devias el alta real escribe dos registros, 0x15e y 0x15f. Sus WORD 2 y 3 distinguen el registro elegido por el
// golpe; comprobar ambos evita leer una copia vieja de AssetsState::fauna para uno de los modelos.
TEST_F(MonsterCombatTest, TheTwoDeviasModelsUseTheirOwnCanonicalHitSoundPairs)
{
    Load(2);
    const std::size_t slot = Create(19);
    ActorState& monster = game->world.Actors()[slot];
    const ModelSounds first = Sounds(0x20, 0x21, 0x41, 0x42, 0x43);
    const ModelSounds second = Sounds(0x30, 0x31, 0x51, 0x52, 0x53);
    game->world.Runtime().modelSounds[0x15e] = first;
    game->world.Runtime().modelSounds[0x15f] = second;

    for (const auto& [model, sounds] :
         {std::pair{std::uint16_t{0x15e}, first}, std::pair{std::uint16_t{0x15f}, second}})
    {
        SCOPED_TRACE(model);
        monster.type = model;
        monster.animation = {.action = 3, .frame = 0.0f};
        constexpr std::uint32_t HitSeed = 0x1357;
        game->cloth.random = CrtRandom(HitSeed);
        game->world.Runtime().lastSoundId.reset();
        CrtRandom expected(HitSeed);
        const std::size_t word = 2 + static_cast<std::size_t>(expected.Next() & 1);

        const auto shocked = Client::Actors::SetActorShock(monster, 30, game->Resources());
        ASSERT_TRUE(shocked.HasValue()) << shocked.Error();
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{sounds[word] + 0xd2});
        EXPECT_EQ(game->cloth.random.State(), expected.State());
    }
}
} // namespace
