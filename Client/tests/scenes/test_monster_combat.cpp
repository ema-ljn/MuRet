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
#include "Skills/AttackStep.h"
#include "Actors/CharacterStep.h"
#include "Chat/ChatReceive.h"
#include "Combat/CombatReplies.h"
#include "Sound/MonsterSounds.h"
#include "Monsters/MonsterViewport.h"
#include "Actors/MovementReplies.h"
#include "Kernel/SceneId.h"
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
#include "Chat/ChatRuntime.h"
#include "Kernel/ModelSounds.h"

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

// The 0x13 monsters in combat: the creation writes its model's sound table (0x612d90), the 0x18 with 0x78 makes them
// hit (0x4f4a10 with its helpers) and count in the step (0x4fd5f0 with 0x4f97b0 and 0x4f5b70), and the D7, the 0x16
// and the 0x17 hurt and kill them (0x4f5660 and 0x4fb9e0). Lab Data (Lorencia and Devias), made-up keys and a fake
// texture table for the monster's model.
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
// Monster10.bmd (0x612d90(9)).
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

// The map's library behind AssetsState::fauna: Player.bmd, also as the Lorencia monster's model.
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

// On the heap: WorldState is about 350 KB.
struct Game
{
    Game()
    {
        // 0x612d90 uploads the textures of the models it loads through the table the game binds.
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
    // Before the world: FaunaModels releases its textures when destroyed.
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

    // In the first free cell from (100, 100); returns its slot.
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

// 0x612d90 from 0x5141e0: Lorencia's 3 gets the table of case 9 (0x15b) and Devias's 19, case 0xc, those of the two
// yeti models (0x15e and 0x15f).
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

// 0x18 with 0x78: 0x4f4a10 without helpers (Lorencia) sets 3, counts +0x38f and asks for the hit sound from its table
// (rand() % 2 between WORD 2 and 3, plus 0xd2); then +0x381 to 1. The attack step counts like a player's and, at
// frame 5, runs the skill on the hero, with the hands sound of the own record.
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
        const auto stepped = Client::Skills::StepTheAttack(monster, slot, context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(monster.byte381, step + 1);
    }
    // Each step: the read of 0x4f97b0, the rand() of 0x4f5b97 and the two reads of 0x4f811b and 0x4f8a2c.
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4);

    monster.animation.frame = 5.0f;
    const auto struck = Client::Skills::StepTheAttack(monster, slot, context);
    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(monster.byte381, 0);
    // The step with the own record (4 + 3848) and the skill with a target (3853).
    EXPECT_EQ(DrawsSince(before, game->cloth.random), 3 * 4 + 4 + 0xf08 + 0xf08 + 5);
    EXPECT_EQ(sounds.size(), 1u);
}

// D7, 0x16 with bit 15 and 0x17 on the monster: the damage with the hit (0x4f53a0, the sound from its table), 0x4f5660
// (the die action and the sound of WORD 4) and the dying one, which 0x4fb9e0 carries until it is erased.
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
    // 0x4f53a0: action 5 and, with the frame at 0, the hit of WORD 2 or 3 of its table (0x4f553d..0x4f55ac).
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
            monster, slot, {.map = 0, .scene = Client::Kernel::SceneId::Game, .random = game->cloth.random});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ++steps;
    }
    EXPECT_FALSE(monster.alive);
    EXPECT_GT(steps, 50);
}

// After a plain hit the capture delivers the five-byte 0x17 first and then the 0x16 without shock. The readers get the
// payloads in clear, after network decryption.
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
            {.map = 0, .scene = Client::Kernel::SceneId::Game, .random = game->cloth.random, .world = &game->world});
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ++steps;
    }
    EXPECT_FALSE(monster.alive);
    EXPECT_GT(steps, 50);
}

// 0x4f553d..0x4f55bb and 0x4f55c0..0x4f5649: the canonical record's table drives the hit sound; type 0x18f keeps five
// dust chances and always ends with sound 0x86, even with the pool full.
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
        static_cast<void>(expectedRandom.Next()); // The hit sound comes before the spark loop.
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

// 0x612d90 loads the same model and sound record that the attack and the death use. A write to
// WorldRuntime.modelSounds after the creation is the canonical table for the hit (0x4f553d..0x4f55a7).
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

// 0x4f5496: a nonzero frame skips the sound; with NaN both ordered jumps are false and it takes its rand(). Either
// way, the five odd gates still create no dust and the 0x18f ends with 0x86.
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

// In Devias the real creation writes two records, 0x15e and 0x15f. Their WORD 2 and 3 tell which record the hit chose;
// checking both avoids reading a stale copy of AssetsState::fauna for one of the models.
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
