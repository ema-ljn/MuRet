
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/ActorSearch.h"
#include "Kernel/PlayerKey.h"
#include "Kernel/CharacterAppearance.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Actors/In/Transformed.h"
#include "Actors/In/ViewportLeave.h"
#include "Actors/In/ViewportPlayers.h"
#include "Actors/In/ViewportPosition.h"
#include "Actors/ViewportHooks.h"
#include "Actors/ActionReply.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Kernel/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameMessages.h"
#include "Scenes/HelperTable.h"
#include "Assets/FaunaModels.h"
#include "Scenes/LoginModels.h"
#include "Actors/MovementReplies.h"
#include "Scenes/SelectionState.h"
#include "Actors/ActorsState.h"
#include "Actors/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Camera/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "Session/ModuleStates.h"
#include "Session/ViewportHooks.h"
#include "Audio/SoundId.h"

#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportHandlers.h"
#include "support/ViewportPackets.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The viewport's packets on the world: the 0x12, the 0x14, the 0x45, the 0x25 and the 0xDF, built with the ASM's
// structs (support/ViewportPackets.h), in Lorencia (0) and Devias (2). The names are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Protocol::Actors::Players;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::AfterDraws;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
// 0x511290 from zero: the two writes of the cell, its four reads and the counter's, 4 rand() each.
constexpr int ConstructionDraws = 28;
// Two accesses to the protected cell (+0x41c and +0x420).
constexpr int CellDraws = 8;
constexpr int FreshRecordDraws = ConstructionDraws + CellDraws + CellDraws;

// The game's world, on the heap: WorldState is about 350 KB.
struct Game
{
    Game()
    {
        // 0x612d90 and 0x612270 upload the textures of the models they load through the table the game binds.
        states.assets.fauna.TextureTable().Bind(store);
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        world.Runtime().hero = 0;
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
    // Before the world: FaunaModels releases its textures when destroyed.
    Client::Tests::FakeTextureStore store;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    // The lab's Data folder, for the Monster*.bmd that the creation of 0x5141e0 looks at; empty outside the tests with
    // data.
    std::filesystem::path dataRoot;
    Client::Scenes::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    // Item.bmd with nothing marked, for 0x54efe0 from 0x512a30.
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    // Without any set: 0x40e1b0 (from 0x512a30) walks the equipment and applies nothing.
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

struct Cell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

Players Read(std::span<const ViewportPlayerRecord> records)
{
    auto read = Client::Protocol::Actors::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

class ViewportPlayersTest : public Client::Tests::GoldenDataTest
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

    // The first cell, from (100, 100), with the four to its right free: no walls and no safe zone.
    [[nodiscard]] Cell FreeRow() const
    {
        const auto& cells = game->states.terrain.current.attributes->cells;
        for (int y = 100; y < 200; ++y)
        {
            for (int x = 100; x < 200; ++x)
            {
                bool free = true;
                for (int step = 0; step < 5 && free; ++step)
                    free = cells[Client::Data::Terrain::CellIndex(Byte(x + step), Byte(y))] == 0;
                if (free)
                    return {Byte(x), Byte(y)};
            }
        }
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    [[nodiscard]] ActorState& Actor(std::size_t slot)
    {
        return game->world.Actors()[slot];
    }

    std::unique_ptr<Game> game;
};

// 0x4c8f4b..0x4c905c: just arrived, at the center of its cell, with effect 0x61f and the alpha at 0; the record's
// target does not count. The queue stores the whole packet before the records.
TEST_F(ViewportPlayersTest, ANewPlayerAppearsInTheCentreOfItsCell)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0456,
                                                            .isNew = true,
                                                            .x = cell.x,
                                                            .y = cell.y,
                                                            .classAndState = 0x20,
                                                            .name = "Hornero",
                                                            .targetX = Byte(cell.x + 3),
                                                            .targetY = cell.y,
                                                            .directionAndPk = 0x53}}};
        const Players players = Read(records);
        const auto applied = Client::Tests::ApplyViewportPlayers(players, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.key, 0x0456);
        EXPECT_EQ(actor.type, PlayerSkeletonModel);
        // ((0x20 & 0x10) | (0x20 >> 4)) >> 1.
        EXPECT_EQ(actor.characterClass, 1);
        EXPECT_EQ(actor.byte88, 1);
        EXPECT_EQ(actor.pk, 3);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(actor.cellY, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.targetY, cell.y);
        EXPECT_EQ(actor.position.x, (static_cast<float>(cell.x) + 0.5f) * 100.0f);
        EXPECT_EQ(actor.position.y, (static_cast<float>(cell.y) + 0.5f) * 100.0f);
        // (5 − 1) · 45.
        EXPECT_EQ(actor.angles.z, 180.0f);
        EXPECT_EQ(actor.alpha, 0.0f);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'H');
        EXPECT_EQ(std::to_integer<char>(actor.name[7]), '\0');
        EXPECT_EQ(actor.byte1e3, 0);
        const auto& effect = game->world.Map().effects.Effects()[0];
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, Client::World::ArrivalEffect);
        EXPECT_TRUE(effect.hasOwner);
        EXPECT_EQ(effect.ownerContext, &actor);
        EXPECT_EQ(effect.disturbanceState, 0xffffu);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        // The session makes the dispatcher's copy (0x4e372e) before the reader; 0x4c8860 copies only with state 1.
        EXPECT_EQ(game->states.actors.viewportPackets.count, 0);
    }
}

// 0x4c907c..0x4c91a3: one already there walks to +0x26/+0x27 from its cell, with the Y read before the X.
TEST_F(ViewportPlayersTest, AnOldPlayerWalksTowardsItsTarget)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0457,
                                                            .x = cell.x,
                                                            .y = cell.y,
                                                            .classAndState = 0x00,
                                                            .name = "Carancho",
                                                            .targetX = Byte(cell.x + 3),
                                                            .targetY = cell.y,
                                                            .directionAndPk = 0x30}}};
        const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.movement, 1);
        ASSERT_GT(actor.path.count, 1);
        EXPECT_EQ(actor.path.cells[0].x, cell.x);
        EXPECT_EQ(actor.path.cells[actor.path.count - 1].x, cell.x + 3);
        EXPECT_EQ(actor.path.cells[actor.path.count - 1].y, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.angles.z, 90.0f);
        EXPECT_EQ(actor.pk, 0);
        EXPECT_FALSE(game->world.Map().effects.Effects()[0].alive);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    }
}

// 0x4e670f..0x4e67a1: 0x400 and 0x10 turn off the entering player's 0x5ac/2 and /1 and create another with it as owner,
// without rand() and without looking at whether it already had the bit; then they set the bit and go on (the 0x80,
// here). With the table full, none, and it goes on.
TEST_F(ViewportPlayersTest, TheGuardAndSwellFlagsLightTheirFlaresOnThePlayer)
{
    using Client::World::GuardFlareSubtype;
    using Client::World::SwellFlareSubtype;
    struct Case
    {
        std::uint32_t flags;
        // The subtypes of slots 0 and 1 of the table at the end (−1, dead); slot 0 starts with an old /2 of the
        // entering player.
        std::array<std::int32_t, 2> subtypes;
        // Whether slot 0's is the new one (the old one went off and the creation took its slot).
        bool renewed;
    };
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        for (const Case& test :
             {Case{0x400u, {GuardFlareSubtype, -1}, true}, Case{0x10u, {GuardFlareSubtype, SwellFlareSubtype}, false},
              Case{0x410u, {GuardFlareSubtype, SwellFlareSubtype}, true}})
        {
            SCOPED_TRACE(testing::Message() << int{map} << " " << test.flags);
            Load(map);
            const Cell cell = FreeRow();
            auto& effects = game->world.Map().effects;
            // The entering player goes to the characters' slot 1.
            ASSERT_TRUE(effects.CreateSkillFlare({}, {}, {}, GuardFlareSubtype, &Actor(1)).has_value());
            const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0458,
                                                                .x = cell.x,
                                                                .y = cell.y,
                                                                .effects = test.flags | 0x80u,
                                                                .name = "Chingolo",
                                                                .targetX = cell.x,
                                                                .targetY = cell.y}}};
            const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
            ASSERT_TRUE(applied.HasValue()) << applied.Error();

            const ActorState& actor = Actor(1);
            EXPECT_EQ(actor.flags78, test.flags | 0x80u);
            const auto live = effects.Effects();
            for (std::size_t slot = 0; slot < test.subtypes.size(); ++slot)
            {
                SCOPED_TRACE(slot);
                const auto& effect = live[slot];
                ASSERT_EQ(effect.alive, test.subtypes[slot] >= 0);
                if (!effect.alive)
                    continue;
                EXPECT_EQ(effect.type, Client::World::IcarusFlareEffect);
                EXPECT_EQ(effect.subtype, test.subtypes[slot]);
                EXPECT_EQ(effect.ownerContext, &actor);
                EXPECT_EQ(effect.life, 1000);
                const bool fresh = slot > 0 || test.renewed;
                // The new one, where 0x511290 left it (the old record does not move it); the old one, at the origin.
                EXPECT_EQ(effect.transform.position == actor.position, fresh);
            }
            EXPECT_FALSE(live[2].alive);
            EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        }
    }
}

TEST_F(ViewportPlayersTest, TheFlaresOfTheFlagsWithAFullTableAreNoneAndTheRestFollows)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        auto& effects = game->world.Map().effects;
        while (effects.Create(Client::World::ArrivalEffect, {}, {}, {}, 0.0f))
        {
        }
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0459, .x = cell.x, .y = cell.y, .effects = 0x410u | 0x1000000u, .name = "Chingolo"}}};
        const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(1).flags78, 0x410u | 0x1000000u);
        for (const auto& effect : effects.Effects())
            EXPECT_EQ(effect.type, Client::World::ArrivalEffect);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    }
}

// 0x4c88d2 (0x576960 with "webzen", case-sensitive): the record does nothing, but the packet is already in the queue.
TEST_F(ViewportPlayersTest, AWebzenNameIsSkippedAfterThePacketIsStored)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 3> records{{
        {.key = 0x0010, .isNew = true, .x = cell.x, .y = cell.y, .name = "elwebzen"},
        {.key = 0x0011, .isNew = true, .x = cell.x, .y = cell.y, .name = "Tero"},
        {.key = 0x0012, .isNew = true, .x = cell.x, .y = cell.y, .name = "WEBZEN"},
    }};
    const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Client::Kernel::ActorWithKey(game->world, 0x0010), Client::Actors::CreatedActorSearch);
    EXPECT_EQ(Client::Kernel::ActorWithKey(game->world, 0x0011), 1u);
    EXPECT_EQ(Client::Kernel::ActorWithKey(game->world, 0x0012), 2u);
    EXPECT_EQ(game->states.actors.viewportPackets.count, 0);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * FreshRecordDraws).State());
}

// 0x4c88e2..0x4c89ed, 0x4c8a43..0x4c8ad4 and 0x4c9229..0x4c9246: state 1 keeps what the one already there had (of
// +0x1f2 only the low byte, Q-C800), starts the teleport (0x53d930, with the own record opened and closed), stores the
// packet again (Q-C801) and at the end overwrites +0x360 and +0x78 with the earlier values.
TEST_F(ViewportPlayersTest, TheTeleportStateKeepsWhatTheOldOneHad)
{
    Load(0);
    const Cell cell = FreeRow();
    ActorState& old = Actor(3);
    old.alive = true;
    old.key = 0x0222;
    old.type = PlayerSkeletonModel;
    old.word1f2 = 0x1234;
    old.guildStatus = 0x80;
    old.byte369 = 1;
    old.byte370 = 2;
    old.byte360 = 5;
    old.flags78 = 8;
    old.byte372 = 3;
    const std::array<ViewportPlayerRecord, 1> records{{{.key = 0x0222,
                                                        .isNew = true,
                                                        .x = cell.x,
                                                        .y = cell.y,
                                                        .classAndState = 0x21,
                                                        .effects = 1,
                                                        .name = "Benteveo",
                                                        .directionAndPk = 0x10}}};
    const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const ActorState& actor = Actor(3);
    EXPECT_FALSE(Actor(1).alive);
    EXPECT_EQ(actor.word1f2, 0x34);
    EXPECT_EQ(actor.guildStatus, 0x80);
    EXPECT_EQ(actor.byte369, 1);
    EXPECT_EQ(actor.byte370, 2);
    EXPECT_EQ(actor.byte372, 3);
    // 0x5139f0 left it at 0 and 0x4e6380 set bit 1: the earlier values overwrite them.
    EXPECT_EQ(actor.byte360, 5);
    EXPECT_EQ(actor.flags78, 8u);
    EXPECT_EQ(actor.animation.action, 0x93);
    EXPECT_EQ(actor.byte80, 3);
    EXPECT_EQ(actor.alphaTarget, 1.0f);
    EXPECT_EQ(actor.alpha, 0.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x76});
    const auto effects = game->world.Map().effects.Effects();
    EXPECT_EQ(effects[0].type, Client::World::TeleportEffect);
    EXPECT_EQ(effects[1].type, Client::World::ArrivalEffect);
    EXPECT_FALSE(effects[0].hasOwner);
    EXPECT_EQ(effects[0].ownerContext, nullptr);
    EXPECT_TRUE(effects[1].hasOwner);
    EXPECT_EQ(effects[1].ownerContext, &actor);
    EXPECT_EQ(effects[1].disturbanceState, 0xffffu);
    // 0x4c8a43..0x4c8ad4: state 1 copies the whole packet (Q-C801), besides the dispatcher's copy.
    const auto& ring = game->states.actors.viewportPackets;
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 5 + 0x2c);
    EXPECT_EQ(std::to_integer<std::uint8_t>(ring.slots[0][3]), 0x12);
    EXPECT_EQ(game->cloth.random.State(),
              AfterDraws(Seed, FreshRecordDraws + Client::Models::OwnRecordCloseRandoms).State());
}

// 0x4c8a36: states 2 to 4 with their action, and the elf's (class & 7 at 2).
TEST_F(ViewportPlayersTest, TheStatesTwoToFourPutTheirActions)
{
    struct Case
    {
        std::uint8_t state;
        std::uint8_t others;
        std::uint8_t elf;
    };
    for (const Case tested : {Case{2, 0xc5, 0xc7}, Case{3, 0xcb, 0xcc}, Case{4, 0xc9, 0xca}})
    {
        SCOPED_TRACE(tested.state);
        Load(0);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 2> records{{
            {.key = 0x0031, .isNew = true, .x = cell.x, .y = cell.y, .classAndState = tested.state, .name = "Mago"},
            {.key = 0x0032,
             .isNew = true,
             .x = cell.x,
             .y = cell.y,
             .classAndState = Byte(0x40 | tested.state),
             .name = "Arquera"},
        }};
        const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(1).characterClass, 0);
        EXPECT_EQ(Actor(1).animation.action, tested.others);
        EXPECT_EQ(Actor(2).characterClass, 2);
        EXPECT_EQ(Actor(2).animation.action, tested.elf);
        EXPECT_EQ(game->states.actors.viewportPackets.count, 0);
    }
}

// 0x4c91aa..0x4c91e1: in Kalima (24 to 29 and 36), 0x530860(0x610, position, position, angles, 8, player, 20, −1, 0, 0,
// −1) with the player as owner, without rand(): it is born at its position with z = 300 and its angles, life 100, 20
// points, width 20, blend 4. Then it goes on: the equipment and the name. In Lorencia and Devias, no joint.
TEST_F(ViewportPlayersTest, InKalimaTheEnteringPlayerTakesTheJoint0x610Of8)
{
    for (const std::int32_t map : {0, 2, 24, 29, 36})
    {
        SCOPED_TRACE(map);
        Load(map == 2 ? std::uint8_t{2} : std::uint8_t{0});
        game->world.Runtime().map = map;
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0040, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero", .directionAndPk = 0x30}}};
        const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'H');

        const auto& joints = game->world.DrawTail().joints;
        const bool kalima = map != 0 && map != 2;
        const Client::Scenes::JointState& joint = joints[0];
        ASSERT_EQ(joint.alive, kalima);
        for (std::size_t i = 1; i < joints.size(); ++i)
            ASSERT_FALSE(joints[i].alive) << i;
        if (!kalima)
            continue;
        EXPECT_EQ(joint.type, 0x610);
        EXPECT_EQ(joint.subtype, 8);
        EXPECT_EQ(joint.ownerContext, &actor);
        EXPECT_EQ(joint.ownerKind, Client::Scenes::JointOwnerKind::Actor);
        EXPECT_EQ(joint.position, glm::vec3(actor.position.x, actor.position.y, 300.0f));
        EXPECT_EQ(joint.angles, actor.angles);
        EXPECT_EQ(joint.targetPosition, actor.position);
        EXPECT_EQ(joint.life, 100);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.width, 20.0f);
        EXPECT_EQ(joint.count, -1);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.blend, 4);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(joint.category, 1);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
    }
}

// 0x53801e..0x538055: the step of the 0x610/8 reads the live byte and the position (with z = 300) from the player;
// dead, it goes off.
TEST_F(ViewportPlayersTest, TheKalimaJointFollowsItsPlayerAndDiesWithIt)
{
    Load(0);
    game->world.Runtime().map = 36;
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0041, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    ASSERT_TRUE(Client::Tests::ApplyViewportPlayers(Read(records), game->Resources()).HasValue());
    auto& joints = game->world.DrawTail().joints;
    ActorState& actor = Actor(1);
    actor.position = {1234.0f, 5678.0f, 90.0f};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].position, glm::vec3(1234.0f, 5678.0f, 300.0f));
    EXPECT_EQ(joints[0].life, 99);
    actor.alive = false;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints).HasValue());
    EXPECT_FALSE(joints[0].alive);
}

// With the 500 joints alive, 0x530860 creates nothing and the 0x12 goes on: the equipment and the name, without extra
// rand().
TEST_F(ViewportPlayersTest, InKalimaWithTheJointsFullThePlayerStillEnters)
{
    Load(0);
    game->world.Runtime().map = 24;
    auto& joints = game->world.DrawTail().joints;
    for (auto& joint : joints)
    {
        joint.alive = true;
        joint.type = 0x617;
    }
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0042, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, FreshRecordDraws).State());
    EXPECT_EQ(std::to_integer<char>(Actor(1).name[0]), 'H');
    for (const auto& joint : joints)
        EXPECT_EQ(joint.type, 0x617);
}

// 0x4c8968 → 0x512870: with the 400 alive, the creation gives 400 and the record writes on it, the pool's 401st.
TEST_F(ViewportPlayersTest, AFullPoolWritesTheNoneRecord)
{
    Load(0);
    const Cell cell = FreeRow();
    for (std::size_t slot = 0; slot < Client::Actors::CreatedActorSearch; ++slot)
    {
        Actor(slot).alive = true;
        Actor(slot).key = static_cast<std::uint16_t>(0x1000 + slot);
    }
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0040, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
    const auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& none = Actor(Client::Actors::CreatedActorSearch);
    EXPECT_FALSE(none.alive);
    EXPECT_EQ(none.cellX, cell.x);
    EXPECT_EQ(none.byte88, 1);
    EXPECT_EQ(std::to_integer<char>(none.name[0]), 'H');
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws + CellDraws).State());
}

// 0x4e3736..0x4e377e: with ten stored, the eleventh shifts slots 1..9 to 0..8 and goes into 9.
TEST(ViewportPacketRingTest, TheEleventhPacketShiftsTheSlots)
{
    Client::Actors::ViewportPacketRing ring;
    for (int packet = 0; packet < 11; ++packet)
    {
        std::array<std::byte, 6> bytes{};
        bytes[5] = std::byte{Byte(packet)};
        Client::Actors::StoreViewportPacket(ring, bytes);
    }
    EXPECT_EQ(ring.count, 10);
    EXPECT_EQ(std::to_integer<int>(ring.slots[0][5]), 1);
    EXPECT_EQ(std::to_integer<int>(ring.slots[8][5]), 9);
    EXPECT_EQ(std::to_integer<int>(ring.slots[9][5]), 10);
    // One over 0x100 does not go in, but shifts the slots anyway.
    const std::array<std::byte, 0x101> large{};
    Client::Actors::StoreViewportPacket(ring, large);
    EXPECT_EQ(ring.count, 9);
    EXPECT_EQ(std::to_integer<int>(ring.slots[0][5]), 2);
    EXPECT_EQ(std::to_integer<int>(ring.slots[8][5]), 10);
    // One of exactly 0x100 goes in (JG): the queue is back to ten.
    std::array<std::byte, 0x100> full{};
    full.back() = std::byte{0xbb};
    Client::Actors::StoreViewportPacket(ring, full);
    EXPECT_EQ(ring.count, 10);
    EXPECT_EQ(ring.sizes[9], 0x100);
    EXPECT_EQ(ring.slots[9].back(), std::byte{0xbb});
}

// 0x4e3778..0x4e377e: with room, one over 0x100 shifts nothing and does not touch what is stored.
TEST(ViewportPacketRingTest, AnOversizedPacketDoesNotEraseAShorterRing)
{
    Client::Actors::ViewportPacketRing ring;
    const std::array<std::byte, 3> teleport{std::byte{0xc1}, std::byte{3}, std::byte{0x1c}};
    Client::Actors::StoreViewportPacket(ring, teleport);
    const std::array<std::byte, 0x101> large{};
    Client::Actors::StoreViewportPacket(ring, large);
    EXPECT_EQ(ring.count, 1);
    EXPECT_EQ(ring.sizes[0], 3);
    EXPECT_EQ(ring.slots[0][2], std::byte{0x1c});
}

// 0x4ca780: the full key 0x13d clears +4..+6 of the siege information, any other +0x1f..+0x21 (Q-C802); the removal
// goes without bit 15 and turns off the helpers of the one leaving.
TEST(ViewportLeaveTest, TheLeaveRemovesWithoutTheNewBitAndClearsTheSiegeHalf)
{
    const auto game = std::make_unique<Game>();
    auto& world = game->world;
    ActorState& leaving = world.Actors()[2];
    leaving = Client::Tests::UnarmedPlayer();
    leaving.alive = true;
    leaving.key = 0x0123;
    leaving.darkSpirit.emplace();
    const auto helper = Client::Scenes::CreateHelper(world, 0xc6, leaving.position, 2);
    ASSERT_TRUE(helper.HasValue()) << helper.Error();
    ASSERT_TRUE(helper.Value().has_value());
    auto& info = world.Runtime().castleSiege.castleSiegeInfo;
    info.emplace();
    info->fill(0xff);

    Client::Tests::ApplyViewportLeave(Client::Protocol::Actors::Leave{{0x8123, 0x013d}}, world);
    EXPECT_FALSE(leaving.alive);
    EXPECT_FALSE(leaving.darkSpirit.has_value());
    EXPECT_FALSE(world.Helpers()[*helper.Value()].alive);
    EXPECT_EQ((*info)[3], 0xff);
    EXPECT_EQ((*info)[4], 0);
    EXPECT_EQ((*info)[6], 0);
    EXPECT_EQ((*info)[7], 0xff);
    EXPECT_EQ((*info)[0x1e], 0xff);
    EXPECT_EQ((*info)[0x1f], 0);
    EXPECT_EQ((*info)[0x21], 0);
    EXPECT_EQ((*info)[0x22], 0xff);
    EXPECT_TRUE(world.Actors()[0].alive);
    // Without the siege information, only the removal.
    info.reset();
    world.Actors()[5].alive = true;
    world.Actors()[5].key = 0x0007;
    Client::Tests::ApplyViewportLeave(Client::Protocol::Actors::Leave{{0x0007}}, world);
    EXPECT_FALSE(world.Actors()[5].alive);
}

// 0x4c92a0: "webzen" does nothing; the other gets the creation of 0x5141e0 with its type (3: Monster10.bmd and model
// 0x15b) and FinishTheTransformed gives it the hero's class read before. The rand(): the creation's 28, the two cells
// and the path.
TEST_F(ViewportPlayersTest, TheTransformedIsCreatedByItsType)
{
    Load(0);
    const std::array<Client::Tests::TransformedRecordBytes, 1> hidden{{{.key = 0x0456, .name = "webzen"}}};
    auto read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(hidden));
    ASSERT_TRUE(read.HasValue());
    EXPECT_TRUE(Client::Tests::ApplyTransformed(read.Value(), game->Resources()).HasValue());
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
    const Cell cell = FreeRow();
    Actor(0).characterClass = 3;
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{{{.key = 0x0456,
                                                                       .x = cell.x,
                                                                       .y = cell.y,
                                                                       .type = 3,
                                                                       .name = "Tatu",
                                                                       .targetX = cell.x,
                                                                       .targetY = cell.y,
                                                                       .directionAndPk = 0x31}}};
    read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto applied = Client::Tests::ApplyTransformed(read.Value(), game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.type, 0x15b);
    EXPECT_EQ(actor.word376, 3);
    EXPECT_EQ(actor.key, 0x0456);
    EXPECT_EQ(actor.characterClass, 3);
    EXPECT_EQ(actor.pk, 1);
    EXPECT_EQ(actor.byte88, 1);
    EXPECT_EQ(actor.byte3e3, 1);
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'T');
    EXPECT_EQ(actor.angles.z, 90.0f);
}

// 0x4c934c: type 68 asks for Monster50.bmd, which the lab's Data does not have: the original closes (P-C173).
TEST_F(ViewportPlayersTest, TheTransformedIntoTheMissingMonsterCuts)
{
    Load(0);
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{{{.key = 0x0456, .x = 130, .y = 130, .type = 68}}};
    const auto read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto applied = Client::Tests::ApplyTransformed(read.Value(), game->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Monster50.bmd"), std::string::npos) << applied.Error();
    EXPECT_FALSE(Actor(1).alive);
}

// 0x4c9356..0x4c975d on the character the creation gave: with +0x376 at 7, the scale at 0.8; the hero's class, the PK,
// +0x88 and +0x3e3; the path with the Y read before the X, the name and the hero dressed again. One just arrived does
// not walk: with the position and the alpha set, circle 0xec (0x4c965c) and ray 0x5d5/2 (0x4c9674) with it as owner,
// without rand(), and then the name.
TEST_F(ViewportPlayersTest, FinishingTheTransformedWalksOrRaisesItsCircleWhenItArrives)
{
    Load(0);
    const Cell cell = FreeRow();
    const Client::Actors::ActorConstructionContext construction{
        game->states.terrain.current, 0, Client::Terrain::CullingScene::Game, game->camera.fixedHeight};
    auto created = Client::Actors::CreateActor(game->world, 0x0456, PlayerSkeletonModel, {cell.x, cell.y, 0.0f},
                                               construction, game->cloth);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 1u);
    Actor(1).word376 = 7;
    Client::Protocol::Actors::TransformedRecord record{.key = 0x0456,
                                                       .x = cell.x,
                                                       .y = cell.y,
                                                       .type = 0x0123,
                                                       .targetX = Byte(cell.x + 2),
                                                       .targetY = cell.y,
                                                       .directionAndPk = 0x31};
    record.name[0] = std::byte{'T'};
    const auto walked = Client::Tests::FinishTheTransformed(1, record, 2, game->Resources());
    ASSERT_TRUE(walked.HasValue()) << walked.Error();
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.scale, 0.8f);
    EXPECT_EQ(actor.characterClass, 2);
    EXPECT_EQ(actor.pk, 1);
    EXPECT_EQ(actor.byte88, 1);
    EXPECT_EQ(actor.byte3e3, 1);
    EXPECT_EQ(actor.angles.z, 90.0f);
    EXPECT_EQ(actor.movement, 1);
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'T');

    record.isNew = true;
    record.name[0] = std::byte{'U'};
    const CrtRandom before = game->cloth.random;
    const auto arrived = Client::Tests::FinishTheTransformed(1, record, 2, game->Resources());
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(std::to_integer<char>(actor.name[0]), 'U');
    const auto effects = std::as_const(game->world.Map().effects).Effects();
    const auto circle = std::ranges::find_if(effects, [](const Client::World::ModelEffect& effect)
                                             { return effect.alive && effect.type == 0xec; });
    ASSERT_NE(circle, effects.end());
    EXPECT_EQ(circle->subtype, 0);
    EXPECT_EQ(circle->ownerContext, &actor);
    EXPECT_EQ(circle->life, 30);
    EXPECT_EQ(circle->transform.position, actor.position);
    const auto& particles = std::as_const(game->world.Particles()).All();
    const auto bolt = std::ranges::find_if(particles, [](const Client::World::Particle& particle)
                                           { return particle.alive && particle.type == 0x5d5; });
    ASSERT_NE(bolt, particles.end());
    EXPECT_EQ(bolt->subtype, 2);
    EXPECT_EQ(bolt->typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::Actor, 1}));
    EXPECT_EQ(actor.alpha, 0.0f);
    EXPECT_EQ(actor.position.x, (static_cast<float>(cell.x) + 0.5f) * 100.0f);
    CrtRandom expected = before;
    for (int draw = 0; draw < CellDraws + CellDraws; ++draw)
        static_cast<void>(expected.Next());
    // The circle and the ray draw no rand(): after the cells only those of 0x512a30 on the published hero are left,
    // which repeat from there.
    const CrtRandom arrivedRandom = game->cloth.random;
    game->cloth.random = expected;
    const auto resources = game->Resources();
    ASSERT_TRUE(Client::Scenes::DressTransitionHero(Client::Scenes::PublishedHero(resources), resources).HasValue());
    EXPECT_EQ(arrivedRandom.State(), game->cloth.random.State());
}

// 0x4e68e1..0x4e68fd: the hero gets Text[1828] every time, even if it already has the flag; the OR runs after the
// notice.
TEST_F(ViewportPlayersTest, TheHeroesBlessingFlagShowsText1828OnEveryApplication)
{
    Load(0);
    const Cell cell = FreeRow();
    const auto texts = Client::Tests::MakeTextTable({{1828, "Bendicion"}});
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer measure{sprites};
    auto resources = game->Resources();
    resources.texts = &texts;
    Client::Scenes::ChatMessages messages{game->world.Runtime(), measure,
                                          [] { return Client::Core::PixelSize{640, 480}; }};
    resources.onScreenMessages = &messages;
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = BlessingFlag, .targetX = cell.x, .targetY = cell.y};
    for (std::size_t count = 1; count <= 2; ++count)
    {
        const auto applied = Client::Tests::FinishTheTransformed(0, record, 0, resources);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const auto& entries = game->world.Runtime().chat.log.entries;
        ASSERT_EQ(entries.size(), count);
        EXPECT_TRUE(entries.front().name.empty());
        EXPECT_EQ(entries.front().text, "Bendicion");
        EXPECT_EQ(entries.front().type, 1);
        EXPECT_NE(Actor(0).flags78 & BlessingFlag, 0u);
    }
}

TEST_F(ViewportPlayersTest, TheBlessingFlagOnAnotherActorNeedsNoMessageResources)
{
    Load(0);
    const Cell cell = FreeRow();
    Actor(1) = Client::Tests::UnarmedPlayer(1);
    Actor(1).alive = true;
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = 0x456, .x = cell.x, .y = cell.y, .effects = BlessingFlag, .targetX = cell.x, .targetY = cell.y};
    const auto applied = Client::Tests::FinishTheTransformed(1, record, 0, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(1).flags78 & BlessingFlag, BlessingFlag);
    EXPECT_TRUE(game->world.Runtime().chat.log.entries.empty());
}

TEST_F(ViewportPlayersTest, AMissingBlessingTextKeepsThePrefixAndLeavesTheFinalFlagUnset)
{
    Load(0);
    const Cell cell = FreeRow();
    constexpr std::uint32_t BlessingFlag = 0x2000000;
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = BlessingFlag | 1u, .targetX = cell.x, .targetY = cell.y};
    const auto applied = Client::Tests::FinishTheTransformed(0, record, 0, game->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Text.bmd"), std::string::npos) << applied.Error();
    EXPECT_EQ(Actor(0).flags78 & 1u, 1u);
    EXPECT_EQ(Actor(0).flags78 & BlessingFlag, 0u);
    EXPECT_TRUE(game->world.Runtime().chat.log.entries.empty());
}

// An empty Text[1828] still goes through 0x555190: it ages the log and adds a row to the old area.
TEST_F(ViewportPlayersTest, AnEmptyBlessingTextStillUpdatesTheMessageAreaAndTheLogAge)
{
    Load(0);
    const Cell cell = FreeRow();
    const auto texts = Client::Tests::MakeTextTable({});
    Client::Render::SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer measure{sprites};
    auto resources = game->Resources();
    resources.texts = &texts;
    Client::Scenes::ChatMessages messages{game->world.Runtime(), measure, resources.screen};
    resources.onScreenMessages = &messages;
    auto& chat = game->world.Runtime().chat;
    chat.log.entries.push_back({.name = "", .text = "previo"});
    const Client::Protocol::Actors::TransformedRecord record{
        .key = HeroKey, .x = cell.x, .y = cell.y, .effects = 0x2000000, .targetX = cell.x, .targetY = cell.y};

    const auto applied = Client::Tests::FinishTheTransformed(0, record, 0, resources);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    ASSERT_EQ(chat.log.entries.size(), 1u);
    EXPECT_EQ(chat.log.entries.front().age, 1u);
    ASSERT_EQ(chat.area.count, 1);
    EXPECT_TRUE(chat.area.rows[0].name.empty());
    EXPECT_TRUE(chat.area.rows[0].text.empty());
    EXPECT_EQ(chat.area.clock, Client::UI::MessageClockSteps);
}

// The 0x25 and the 0xDF on a player already there.
class ShownEquipmentTest : public ::testing::Test
{
protected:
    ShownEquipmentTest()
    {
        ActorState& actor = Other();
        actor = Client::Tests::UnarmedPlayer(1);
        actor.alive = true;
        actor.key = OtherKey;
    }
    ActorState& Other()
    {
        return game->world.Actors()[OtherSlot];
    }
    ActorState& Hero()
    {
        return game->world.Actors()[0];
    }
    Client::Core::Expected<void, std::string> Show(std::uint16_t key, const std::array<std::uint8_t, 7>& item)
    {
        auto read = Client::Protocol::Actors::ReadEquipmentShown(Client::Tests::EquipmentShownPacket(key, item));
        EXPECT_TRUE(read.HasValue());
        return Client::Tests::ApplyEquipmentShown(read.Value(), game->Resources());
    }
    static constexpr std::uint16_t OtherKey = 0x0042;
    static constexpr std::size_t OtherSlot = 4;
    std::unique_ptr<Game> game = std::make_unique<Game>();
};

// 0x4c83ec..0x4c842c: the right hand with the model, the level (0x513340 of the nibble; from 8 to 15, 0) and the
// excellent bits.
TEST_F(ShownEquipmentTest, TheRightHandTakesTheModelLevelAndExcellent)
{
    // Type 0x103: byte 0 and bit 0x80 of byte 3; slot 0 and level 3 in byte 1.
    ASSERT_TRUE(Show(OtherKey, {0x03, 0x03, 0, 0x85, 0x07, 0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].model, 0x103 + 0x212);
    EXPECT_EQ(Other().hands[0].level, Client::Kernel::ItemLevelOf(3));
    EXPECT_EQ(Other().hands[0].flags1b, 5);
    ASSERT_TRUE(Show(OtherKey, {0x03, 0x09, 0, 0, 0, 0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].level, 0);
    // Empty (0x1fff): the model at 0xffff and the excellent bits at 0; the level stays.
    Other().hands[0].level = 4;
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x00, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().hands[0].model, Client::Kernel::NoItemModel);
    EXPECT_EQ(Other().hands[0].flags1b, 0);
    EXPECT_EQ(Other().hands[0].level, 4);
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c8431..0x4c8457: emptying the left hand releases the published hero's raven, not this character's (Q-C803).
TEST_F(ShownEquipmentTest, EmptyingTheLeftHandReleasesThePublishedHerosRaven)
{
    Hero().darkSpirit.emplace();
    Other().darkSpirit.emplace();
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x10, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().hands[1].model, Client::Kernel::NoItemModel);
    EXPECT_FALSE(Hero().darkSpirit.has_value());
    EXPECT_TRUE(Other().darkSpirit.has_value());
}

// 0x4c848b..0x4c8667: the body with the set; empty, the class's part.
TEST_F(ShownEquipmentTest, TheBodyTakesTheSetOrTheClassPart)
{
    // The armor (slot 3) of type 0x1005: byte 5 with 8 on top.
    ASSERT_TRUE(Show(OtherKey, {0x05, 0x31, 0, 0x02, 0x05, 0x80, 0}).HasValue());
    EXPECT_EQ(Other().body[1].model, 0x1005 + 0x212);
    EXPECT_EQ(Other().body[1].level, Client::Kernel::ItemLevelOf(1));
    EXPECT_EQ(Other().body[1].flags1b, 2);
    EXPECT_EQ(Other().body[1].flags1c, 5);
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x30, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().body[1].model,
              Client::Kernel::ClassPartModel(Client::Kernel::BodyPart{1}, Other().characterClass));
    EXPECT_EQ(Other().body[1].level, 0);
    EXPECT_EQ(Other().body[1].flags1c, 0);
    // The wings (7) with the level at 0, without looking at the nibble.
    ASSERT_TRUE(Show(OtherKey, {0x01, 0x7f, 0, 0, 0, 0xc0, 0}).HasValue());
    EXPECT_EQ(Other().wings.model, 0x1801 + 0x212);
    EXPECT_EQ(Other().wings.level, 0);
}

// 0x4c8711..0x4c87d4: the pet with its helper; 0x1c12 draws its three rand() and Fenrir keeps the excellent bits.
TEST_F(ShownEquipmentTest, ThePetCreatesItsHelper)
{
    ASSERT_TRUE(Show(OtherKey, {0x00, 0x80, 0, 0, 0, 0xd0, 0}).HasValue());
    EXPECT_EQ(Other().pet.model, 0x1a00 + 0x212);
    const auto& helpers = game->world.Helpers();
    EXPECT_TRUE(helpers[0].alive);
    EXPECT_EQ(helpers[0].type, 0x1c12);
    EXPECT_EQ(helpers[0].rider, OtherSlot);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 3).State());
    // Empty: 0x443d40 turns off its helpers.
    ASSERT_TRUE(Show(OtherKey, {0xff, 0x80, 0, 0x80, 0, 0xf0, 0}).HasValue());
    EXPECT_EQ(Other().pet.model, Client::Kernel::NoItemModel);
    EXPECT_FALSE(helpers[0].alive);
    // Fenrir (0x1a25) with excellent bits 2: helper 0x14c.
    ASSERT_TRUE(Show(OtherKey, {0x25, 0x80, 0, 0x02, 0, 0xd0, 0}).HasValue());
    EXPECT_EQ(Other().pet.flags1b, 2);
    EXPECT_EQ(helpers[0].type, 0x14c);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 3).State());
}

// 0x4c838e → 0x5110d0 with the whole key: with bit 15, nobody; it writes on 400.
TEST_F(ShownEquipmentTest, TheWholeKeyWithTheNewBitFindsNobody)
{
    ASSERT_TRUE(Show(0x8000 | OtherKey, {0x03, 0x00, 0, 0, 0, 0, 0}).HasValue());
    EXPECT_NE(Other().hands[0].model, 0x3 + 0x212);
    EXPECT_EQ(game->world.Actors()[Client::Actors::CreatedActorSearch].hands[0].model, 0x3 + 0x212);
}

// 0x4c6ca8..0x4c6dfe: the protected cell, the target and +0x399.
TEST_F(ShownEquipmentTest, ThePositionWritesTheCells)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    ASSERT_TRUE(Client::Actors::In::ApplyViewportPosition(read.Value(), game->world, game->cloth.random).HasValue());
    EXPECT_EQ(Other().cellX, 140);
    EXPECT_EQ(Other().cellY, 141);
    EXPECT_EQ(Other().targetX, 140);
    EXPECT_EQ(Other().targetY, 141);
    EXPECT_EQ(Other().byte399, 1);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws).State());
}

// 0x4c6c9d calls 0x51fe10(position, 0): two draws per try, five per actual creation and no 0x5ed.
TEST_F(ShownEquipmentTest, ThePushedTypeCreatesTwentySparksWithoutMovingItsCell)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    Other().type = 0xf1;
    Other().cellX = 138;
    Other().cellY = 143;
    Other().position = {12, 34, 56};
    const auto pushed = Client::Actors::In::ApplyViewportPosition(read.Value(), game->world, game->cloth.random);
    ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
    EXPECT_EQ(Other().floatE4, 20.0f);
    EXPECT_EQ(Other().floatD0, 800.0f);
    EXPECT_EQ(Other().floatCC, 800.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1b});
    EXPECT_EQ(Other().cellX, 138);
    EXPECT_EQ(Other().cellY, 143);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, CellDraws + 20 * 7).State());
    const auto particles = game->world.Particles().All();
    for (std::size_t index = 0; index < particles.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(particles[index].alive, index < 20);
        if (index >= 20)
            continue;
        EXPECT_EQ(particles[index].type, Client::World::FairySparkParticle);
        EXPECT_EQ(particles[index].subtype, 2);
        EXPECT_EQ(particles[index].position, glm::vec3(12, 34, 136));
        EXPECT_EQ(particles[index].light, glm::vec3(1));
        EXPECT_GE(particles[index].angles.x, 150.0f);
        EXPECT_LT(particles[index].angles.x, 210.0f);
    }
}

// 0x51fe4b/0x51fe6e come before the slot search of 0x53fe70. Filling the pool does not stop the other tries.
TEST_F(ShownEquipmentTest, ThePushedTypeKeepsEveryAttemptWithAPartialOrFullPool)
{
    const auto read = Client::Protocol::Actors::ReadPosition(Client::Tests::PositionPacket(OtherKey, 140, 141));
    ASSERT_TRUE(read.HasValue());
    Other().type = 0xf1;
    Other().cellX = 138;
    Other().cellY = 143;
    for (const std::size_t free : {std::size_t{0}, std::size_t{1}, std::size_t{19}})
    {
        SCOPED_TRACE(free);
        auto particles = game->world.Particles().All();
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            particles[index].alive = index >= free;
            particles[index].life = 101;
            particles[index].position = {2, 4, 8};
        }
        game->cloth.random = CrtRandom(Seed);
        const auto pushed = Client::Actors::In::ApplyViewportPosition(read.Value(), game->world, game->cloth.random);
        ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
        const int draws = CellDraws + 40 + static_cast<int>(free) * 5;
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, draws).State());
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_TRUE(particles[index].alive);
            if (index < free)
            {
                EXPECT_EQ(particles[index].type, Client::World::FairySparkParticle);
                EXPECT_EQ(particles[index].subtype, 2);
            }
            else
            {
                EXPECT_EQ(particles[index].life, 101);
                EXPECT_EQ(particles[index].position, glm::vec3(2, 4, 8));
            }
        }
    }
}

// Another player's D3 and D7 (0x4c6d60 and 0x4cab40) find the one the 0x12 created.
TEST_F(ViewportPlayersTest, TheWalkAndTheHitOfTheOthersFindTheCreatedPlayer)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0456, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
        auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ActorState& actor = Actor(1);

        applied =
            Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{0x0456, {Byte(cell.x + 3), cell.y}, 2},
                                           Client::Kernel::PlayerKey{HeroKey}, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.targetX, cell.x + 3);
        EXPECT_EQ(actor.direction, 2);
        EXPECT_EQ(actor.movement, 1);

        applied = Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{0x0456, true, 25, 0, false},
                                                   Client::Kernel::PlayerKey{HeroKey}, game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.damage, 25);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_TRUE(game->world.DrawTail().damage[0].alive);
    }
}

// The 18 with action 0x78 of a player the 0x12 created (0x4cb750 → 0x4f4a10): the encoded 0xfff keeps 0x1211 and goes
// through the random sound 0x3c (0x4f5192); with both hands at 0xffff, it only closes the own record. With a sword of a
// group from 0 to 2 without Item.bmd's +0x1e mark in the right hand, 0x23 or 0x24 by +0x38f and the sound rand() & 1 +
// 0x3c (0x4f5192). Then +0x381 at 1, the frame at 0, the target with the hero's slot and [0x5767964] with the index.
TEST_F(ViewportPlayersTest, ACreatedPlayerStrikesWithTheEighteen)
{
    constexpr int OwnRecordDraws = 0xf08;
    constexpr std::uint16_t Sword = 0x212;
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().heroSlot = 0;
        const Cell cell = FreeRow();
        const std::array<ViewportPlayerRecord, 1> records{
            {{.key = 0x0456, .isNew = true, .x = cell.x, .y = cell.y, .name = "Hornero"}}};
        auto applied = Client::Tests::ApplyViewportPlayers(Read(records), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ActorState& actor = Actor(1);
        const std::uint32_t created = game->cloth.random.State();

        applied = Client::Actors::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 5, .action = 0x78}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.animation.action, 0x22);
        EXPECT_EQ(actor.animation.frame, 0.0f);
        EXPECT_EQ(actor.angles.z, 180.0f);
        EXPECT_EQ(actor.byte381, 1);
        EXPECT_EQ(actor.attackCount, 1);
        EXPECT_EQ(actor.attackTarget, 0);
        EXPECT_EQ(game->world.Runtime().lastAttacker, 1);
        EXPECT_EQ(actor.hands[0].model, 0x1211);
        EXPECT_EQ(actor.hands[1].model, 0x1211);
        auto encoded = AfterDraws(created, OwnRecordDraws);
        const std::int32_t encodedSound = (encoded.Next() & 1) + 0x3c;
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{encodedSound});
        EXPECT_EQ(game->cloth.random.State(), encoded.State());

        // 0x4f5181/0x4f5186: only both WORDs at 0xffff skip the rand() and the sound.
        actor.hands[0].model = Client::Kernel::NoItemModel;
        actor.hands[1].model = Client::Kernel::NoItemModel;
        game->world.Runtime().lastSoundId.reset();
        const auto unarmed = AfterDraws(encoded.State(), OwnRecordDraws);
        applied = Client::Actors::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 5, .action = 0x78}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(actor.animation.action, 0x22);
        EXPECT_FALSE(game->world.Runtime().lastSoundId.has_value());
        EXPECT_EQ(game->cloth.random.State(), unarmed.State());

        actor.hands[0].model = Sword;
        actor.animation.action = 0;
        auto expected = AfterDraws(game->cloth.random.State(), OwnRecordDraws);
        const std::int32_t sound = (expected.Next() & 1) + 0x3c;
        applied = Client::Actors::ApplyActionReply(
            Client::Protocol::Movement::Action{.key = 0x0456, .direction = 1, .action = 0x79}, game->Resources(), {});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        // +0x38f at 2 after the two attacks: (2 & 1) + 0x23.
        EXPECT_EQ(actor.animation.action, 0x23);
        EXPECT_EQ(actor.attackCount, 3);
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{sound});
        EXPECT_EQ(game->cloth.random.State(), expected.State());
    }
}

// Each slot the handlers ask, by name and in order, before the session's own.
class RecordingViewportHooks final : public Client::Actors::IViewportHooks
{
public:
    explicit RecordingViewportHooks(const Client::Scenes::WorldEntryResources& resources) : m_real(resources) {}
    [[nodiscard]] Client::Core::Expected<void, std::string> StartTheTeleportAction(ActorState& actor) const override
    {
        m_calls.emplace_back("StartTheTeleportAction");
        return m_real.StartTheTeleportAction(actor);
    }
    void CreateTheArrivalEffect(ActorState& actor) const override
    {
        m_calls.emplace_back("CreateTheArrivalEffect");
        m_real.CreateTheArrivalEffect(actor);
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> CreateTheKalimaBeam(ActorState& actor) const override
    {
        m_calls.emplace_back("CreateTheKalimaBeam");
        return m_real.CreateTheKalimaBeam(actor);
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> RefreshTheSiegePanel() const override
    {
        m_calls.emplace_back("RefreshTheSiegePanel");
        return m_real.RefreshTheSiegePanel();
    }
    [[nodiscard]] std::uint8_t PublishedHeroClass() const override
    {
        m_calls.emplace_back("PublishedHeroClass");
        return m_real.PublishedHeroClass();
    }
    [[nodiscard]] Client::Core::Expected<std::size_t, std::string>
    CreateMonster(std::int32_t type, std::uint8_t x, std::uint8_t y, std::int32_t key) const override
    {
        m_calls.emplace_back("CreateMonster");
        return m_real.CreateMonster(type, x, y, key);
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> CreateTheMagicCircle(ActorState& actor) const override
    {
        m_calls.emplace_back("CreateTheMagicCircle");
        return m_real.CreateTheMagicCircle(actor);
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> DressThePublishedHero() const override
    {
        m_calls.emplace_back("DressThePublishedHero");
        return m_real.DressThePublishedHero();
    }
    void ReleaseThePublishedHerosRaven() const override
    {
        m_calls.emplace_back("ReleaseThePublishedHerosRaven");
        m_real.ReleaseThePublishedHerosRaven();
    }
    [[nodiscard]] Client::Core::Expected<void, std::string> RecreateTheRaven(ActorState& actor) const override
    {
        m_calls.emplace_back("RecreateTheRaven");
        return m_real.RecreateTheRaven(actor);
    }
    void TurnOffTheHelpersOf(std::size_t rider) const override
    {
        m_calls.emplace_back("TurnOffTheHelpersOf");
        m_real.TurnOffTheHelpersOf(rider);
    }
    [[nodiscard]] Client::Core::Expected<void, std::string>
    CreateTheHelper(std::uint16_t type, const glm::vec3& position, std::size_t rider) const override
    {
        m_calls.emplace_back("CreateTheHelper");
        return m_real.CreateTheHelper(type, position, rider);
    }
    [[nodiscard]] const std::vector<std::string>& Calls() const
    {
        return m_calls;
    }

private:
    Client::Session::ViewportHooks m_real;
    mutable std::vector<std::string> m_calls;
};

// 0x4c8860 on a player of state 1 just arrived in Kalima: 0x53d930, effect 0x61f, joint 0x610/8, the 0x443d40 of its
// set (0x5139f0) and the siege's panel, in that order.
TEST_F(ViewportPlayersTest, TheEnteringPlayerAsksItsSlotsInTheOriginalsOrder)
{
    Load(0);
    game->world.Runtime().map = 24;
    const Cell cell = FreeRow();
    const std::array<ViewportPlayerRecord, 1> records{
        {{.key = 0x0050, .isNew = true, .x = cell.x, .y = cell.y, .classAndState = 0x01, .name = "Hornero"}}};
    const auto resources = game->Resources();
    const RecordingViewportHooks hooks(resources);
    const auto applied = Client::Actors::In::ApplyViewportPlayers(Read(records), resources, hooks);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(hooks.Calls(),
              (std::vector<std::string>{"StartTheTeleportAction", "CreateTheArrivalEffect", "CreateTheKalimaBeam",
                                        "TurnOffTheHelpersOf", "RefreshTheSiegePanel"}));
}

// 0x4c92a0 on a transformed one just arrived: the hero's class before the creation, 0x5141e0, circle 0xec and 0x512a30
// on the published hero, in that order.
TEST_F(ViewportPlayersTest, TheTransformedAsksItsSlotsInTheOriginalsOrder)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<Client::Tests::TransformedRecordBytes, 1> shown{
        {{.key = 0x0456, .isNew = true, .x = cell.x, .y = cell.y, .type = 3, .name = "Tatu"}}};
    const auto read = Client::Protocol::Actors::ReadTransformed(Client::Tests::TransformedPacket(shown));
    ASSERT_TRUE(read.HasValue());
    const auto resources = game->Resources();
    const RecordingViewportHooks hooks(resources);
    const auto applied = Client::Actors::In::ApplyTransformed(read.Value(), resources, hooks);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(hooks.Calls(), (std::vector<std::string>{"PublishedHeroClass", "CreateMonster", "CreateTheMagicCircle",
                                                       "DressThePublishedHero"}));
}

// What the 0x14 asks of each key, and whether the character was still there when it asked.
class RecordingLeaveHooks final : public Client::Actors::IViewportLeaveHooks
{
public:
    RecordingLeaveHooks(Client::Kernel::WorldState& world, const ActorState& watched)
        : m_real(world), m_watched(watched)
    {
    }
    void ForgetTheSwitchHolder(std::uint16_t key) const override
    {
        m_calls.push_back(std::format("switch {:#06x} {}", key, m_watched.alive));
        m_real.ForgetTheSwitchHolder(key);
    }
    void RemoveTheShopTitle(std::int32_t key) const override
    {
        m_calls.push_back(std::format("shop {:#06x} {}", key, m_watched.alive));
        m_real.RemoveTheShopTitle(key);
    }
    [[nodiscard]] const std::vector<std::string>& Calls() const
    {
        return m_calls;
    }

private:
    Client::Session::ViewportLeaveHooks m_real;
    const ActorState& m_watched;
    mutable std::vector<std::string> m_calls;
};

// 0x4ca780: per key, the switch holder with the whole key before the removal, and the shop sign without bit 15 after.
TEST(ViewportLeaveTest, EachKeyAsksTheSwitchHolderBeforeTheRemovalAndTheShopSignAfter)
{
    const auto game = std::make_unique<Game>();
    auto& world = game->world;
    ActorState& leaving = world.Actors()[2];
    leaving = Client::Tests::UnarmedPlayer();
    leaving.alive = true;
    leaving.key = 0x0123;
    const RecordingLeaveHooks hooks(world, leaving);
    Client::Actors::In::ApplyViewportLeave(Client::Protocol::Actors::Leave{{0x8123, 0x013d}}, world, hooks);
    EXPECT_EQ(hooks.Calls(), (std::vector<std::string>{"switch 0x8123 true", "shop 0x0123 false", "switch 0x013d false",
                                                       "shop 0x013d false"}));
}
} // namespace
