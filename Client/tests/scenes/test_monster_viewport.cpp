#include "Monsters/MonsterViewport.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/NpcNames.h"
#include "Tables/TextTable.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActorPickingBox.h"
#include "Kernel/ActorSearch.h"
#include "Kernel/ActorState.h"
#include "CastleSiege/CastleBattleReceive.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "Particles/Particles.h"
#include "FrontEnd/LoginModels.h"
#include "Monsters/MonsterCreation.h"
#include "Monsters/MonsterKinds.h"
#include "Monsters/NpcNames.h"
#include "FrontEnd/SelectionState.h"
#include "Movement/GameMovement.h"
#include "Session/HeroMovementHooks.h"
#include "Hud/GameUi.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"
#include "Terrain/TerrainPickingSegment.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/FakeTextureStore.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/Packets.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportPackets.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Events/EventGlobals.h"
#include "Quests/QuestState.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The viewport monsters and NPCs: 0x13, 0x1F and 0x68 on top of the 0x5141e0 creation, with the lab's Data (the
// Monster*.bmd the creation reads and the worlds). The records are built with the ASM structs
// (support/ViewportPackets.h); the owners' names and Text 485 are made up.
namespace
{
const Client::Session::HeroMovementHooks MovementHooks{};
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::AfterDraws;
using Client::Tests::MonsterRecordBytes;
using Client::Tests::ObjectRecordBytes;
using Client::Tests::SummonRecordBytes;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t HeroKey = 33;
constexpr std::int32_t HeroSlot = 5;
// 0x511290 from scratch: the cell's two writes, its four reads and the counter's, 4 rand() each.
constexpr int ConstructionDraws = 28;
// Two accesses to the protected cell (+0x41c and +0x420).
constexpr int CellDraws = 8;
// The creation, the record's two cells and the path's two reads.
constexpr int WalkingRecordDraws = ConstructionDraws + CellDraws + CellDraws;
// The creation and the two cells: a new arrival does not walk.
constexpr int ArrivingRecordDraws = ConstructionDraws + CellDraws;

// On the heap: WorldState weighs about 350 KB.
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
                .texts = texts,
                .states = &states};
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
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemsMarking({});
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    const Client::Tables::TextTable* texts = nullptr;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

struct Cell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

float Real(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

Client::Protocol::Actors::Monsters ReadMonsters(std::span<const MonsterRecordBytes> records)
{
    auto read = Client::Protocol::Actors::ReadMonsters(Client::Tests::MonstersPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

Client::Protocol::Actors::Summons ReadSummons(std::span<const SummonRecordBytes> records)
{
    auto read = Client::Protocol::Actors::ReadSummons(Client::Tests::SummonsPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

Client::Protocol::Actors::Objects ReadObjects(std::span<const ObjectRecordBytes> records)
{
    auto read = Client::Protocol::Actors::ReadObjects(Client::Tests::ObjectsPacket(records));
    EXPECT_TRUE(read.HasValue());
    return std::move(read).Value();
}

class MonsterViewportTest : public Client::Tests::GoldenDataTest
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

    // The first cell from (100, 100) with the four to its right free too: no walls and no safe zone.
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

    [[nodiscard]] Client::Core::Expected<void, std::string> Monsters(std::span<const MonsterRecordBytes> records)
    {
        return Client::Monsters::ApplyViewportMonsters(ReadMonsters(records), game->Resources());
    }

    void ClickVisibleSiegeNpc(const Client::Tables::SkillTable& skills, std::int16_t key)
    {
        auto& runtime = game->world.Runtime();
        auto& npc = Actor(1);
        auto& hero = Actor(0);
        hero.position = npc.position;
        hero.alpha = 1.0f;
        hero.cellX = npc.cellX;
        hero.cellY = npc.cellY;
        // The last draw's state: the created actor's box and a ray through it.
        npc.seen = true;
        npc.alpha = 1;
        Client::Actors::PublishPickingBox(npc, npc.position, npc.boundsMin,
                                          {npc.boundsMaxX, npc.boundsMaxY, npc.boundsTop});
        const glm::vec3 offset{0, 0, 500};
        const auto picked = Client::Hud::PickInTheFrame(
            {.world = game->world,
             .random = game->cloth.random,
             .ownRecordDepth = runtime.ownRecordDepth,
             .targets = runtime.selectedTargets,
             .ray = Client::Terrain::TerrainPickingSegment{npc.position + offset, npc.position - offset},
             .terrain = &game->states.terrain.current});
        ASSERT_TRUE(picked.HasValue()) << picked.Error();
        ASSERT_EQ(runtime.selectedTargets[1], 1);
        ASSERT_EQ(runtime.selectedTargets[3], -1);
        std::vector<std::int16_t> talkKeys;
        runtime.mouse.updateTime = 6;
        const auto clicked = Client::Movement::StepHeroMovement(
            {.world = game->world,
             .random = game->cloth.random,
             .playerSkeleton = game->models.SkeletonOf(Client::Models::ModelId{PlayerSkeletonModel}),
             .items = &game->items,
             .skills = &skills,
             .input = {.leftPressed = true, .leftDown = true},
             .sendTalk = [&](std::int16_t talked) { talkKeys.push_back(talked); },
             .terrain = &game->states.terrain.current,
             .hooks = &MovementHooks});
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        ASSERT_EQ(talkKeys, (std::vector<std::int16_t>{key}));
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Summons(std::span<const SummonRecordBytes> records)
    {
        return Client::Monsters::ApplySummons(ReadSummons(records), game->Resources());
    }

    [[nodiscard]] Client::Core::Expected<void, std::string> Objects(std::span<const ObjectRecordBytes> records)
    {
        return Client::Monsters::ApplyViewportObjects(ReadObjects(records), game->Resources());
    }

    std::unique_ptr<Game> game;
};

// 0x4c9c60 with the switch case: in Lorencia type 3 (0x515a1f) and in Devias 19 (0x51595e) load their .bmd, create the
// model with its box and hands, the name and the scale; the tail sets +0x376, +0x3a4 and class 2. Then the angle times
// 45, the cells, +0x42d and +0x93 of the high type, and the path. The rand(): the creation's 28, the two cells and the
// path's two reads.
TEST_F(MonsterViewportTest, TheMonstersOfLorenciaAndDeviasComeFromTheirCase)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        std::uint16_t model;
        std::uint32_t scale;
        const char* name;
        glm::vec3 box;
    };
    const std::array<Case, 2> cases{{
        {0, 3, 0x15b, 0x3ecccccd, "\xb0\xc5\xb9\xcc", {50.0f, 50.0f, 80.0f}},
        {2, 19, 0x15e, 0x3f8ccccd, "\xbc\xb3\xc0\xce", {50.0f, 50.0f, 150.0f}},
    }};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0123,
                                                          .typeHigh = 0xb0,
                                                          .typeLow = tested.type,
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = Byte(cell.x + 3),
                                                          .targetY = cell.y,
                                                          .direction = 0x35}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        const ActorState& actor = Actor(1);
        EXPECT_TRUE(actor.alive);
        EXPECT_EQ(actor.key, 0x0123);
        EXPECT_EQ(actor.type, tested.model);
        EXPECT_EQ(actor.word376, tested.type);
        EXPECT_EQ(actor.scale, Real(tested.scale));
        EXPECT_EQ(Client::Monsters::ActorText(actor), tested.name);
        EXPECT_EQ(actor.boundsMin, (glm::vec3{-60.0f, -60.0f, 0.0f}));
        EXPECT_EQ((glm::vec3{actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop}), tested.box);
        EXPECT_EQ(actor.hands[0].bone, 0x21u);
        EXPECT_EQ(actor.hands[1].bone, 0x2au);
        EXPECT_EQ(actor.byte88, 2);
        EXPECT_EQ(actor.byte8, 0);
        EXPECT_EQ(actor.attackTarget, HeroSlot);
        EXPECT_EQ(actor.characterClass, 0);
        // (3 − 1) · 45.
        EXPECT_EQ(actor.angles.z, 90.0f);
        EXPECT_EQ(actor.byte42d, 0x80);
        EXPECT_EQ(actor.byte93, 3);
        EXPECT_EQ(actor.cellX, cell.x);
        EXPECT_EQ(actor.cellY, cell.y);
        EXPECT_EQ(actor.targetX, cell.x);
        EXPECT_EQ(actor.movement, 1);
        EXPECT_EQ(actor.alpha, 1.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws).State());
    }
}

// 0x4ca06e → 0x4eac00 (Q-C1351): the path of a monster that is not a new arrival also carries the flag if 0x6c8e54 has
// an altar (record 9, dead) with stage 4 on map 34: its goal, a wall in the TerrainWall, is set to 0 and the path
// arrives. In Lorencia and Devias the path stops next to the goal, which stays as it was.
TEST_F(MonsterViewportTest, AChosenCrywolfAltarAlsoClearsTheGoalOfAMonster)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{34}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        Load(map);
        game->world.Runtime().crywolf.stage = 4;
        game->world.Runtime().chosenNpc.index = 9;
        Actor(9).type = 0x205;
        const Cell cell = FreeRow();
        const Cell target{Byte(cell.x + 4), cell.y};
        auto& goal =
            game->states.terrain.current.attributes->cells[Client::Data::Terrain::CellIndex(target.x, target.y)];
        goal = Client::Pathfinding::BlockedWallBit;
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0123,
                                                          .typeHigh = 0xb0,
                                                          .typeLow = 3,
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = target.x,
                                                          .targetY = target.y,
                                                          .direction = 0x35}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.movement, 1);
        ASSERT_GT(actor.path.count, 1);
        const auto& last = actor.path.cells[actor.path.count - 1U];
        if (map == 34)
        {
            EXPECT_EQ(goal, 0);
            EXPECT_EQ(last.x, target.x);
            EXPECT_EQ(last.y, target.y);
        }
        else
        {
            EXPECT_EQ(goal, Client::Pathfinding::BlockedWallBit);
            EXPECT_FALSE(last.x == target.x && last.y == target.y);
            EXPECT_EQ(std::max(std::abs(last.x - target.x), std::abs(last.y - target.y)), 1);
        }
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws).State());
    }
}

// 0x51411f..0x51416b: with NpcName(%s).txt loaded, the tail copies to +0x1d9 the name of the first record with the type
// (with its '\0': what is left of the case's name is not seen). A type with no record keeps the case's. One of 25
// letters leaves its '\0' in the low byte of the WORD at +0x1f2, like the original's unbounded strcpy (Q-C847); one
// that would reach the parts at +0x218 cuts before +0x376 (P-C352). The names are made up.
TEST_F(MonsterViewportTest, TheTailCopiesTheNameOfTheTable)
{
    const auto put = [](Client::Monsters::NpcName& record, std::uint16_t type, std::string_view name)
    {
        record.type = type;
        record.name = {};
        std::ranges::copy(name, record.name.begin());
    };
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        const char* caseName;
    };
    for (const Case& tested : {Case{0, 3, "\xb0\xc5\xb9\xcc"}, Case{2, 19, "\xbc\xb3\xc0\xce"}})
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        auto& names = game->world.Runtime().npcNames;
        // The tail's 0x54ebb0 compares the unsigned word with the type's dword: the first one wins.
        put(names[4], tested.type, "Bicho");
        put(names[9], tested.type, "Otro bicho");
        const Cell cell = FreeRow();
        std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0123, .typeLow = tested.type, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Bicho");
        EXPECT_EQ(Actor(1).word376, tested.type);

        names[4].type = 0;
        names[9].type = 0;
        records[0].key = 0x0124;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(2)), tested.caseName);

        // 24 letters and the '\0' reach +0x1f1; the WORD at +0x1f2 keeps the 0xffff of 0x511290.
        put(names[4], tested.type, "Bicho de veinticuatro le");
        records[0].key = 0x0125;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(3)), "Bicho de veinticuatro le");
        EXPECT_EQ(Actor(3).word1f2, 0xffff);

        // With one more, the '\0' lands in the low byte of +0x1f2; it does not touch the key at +0x1f4.
        put(names[4], tested.type, "Bicho de veinticinco letr");
        records[0].key = 0x0126;
        applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(4)), "Bicho de veinticinco letr");
        EXPECT_EQ(Actor(4).word1f2, 0xff00);
        EXPECT_EQ(Actor(4).key, 0x0126);
        EXPECT_EQ(Actor(4).word376, tested.type);

        // With no '\0' in its 0x34 bytes, 0x54ebb0 goes on through record 5's type and name: 52 + 2 + 10 letters and
        // the '\0' are 65 bytes from +0x1d9, past +0x217.
        names[4].type = tested.type;
        std::ranges::fill(names[4].name, 'x');
        put(names[5], 0x7979, "zzzzzzzzzz");
        records[0].key = 0x0127;
        applied = Monsters(records);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("0x514164"), std::string::npos) << applied.Error();
        EXPECT_NE(applied.Error().find("P-C352"), std::string::npos) << applied.Error();
        // +0x376 keeps the 0xffff of 0x511290.
        EXPECT_EQ(Actor(5).word376, 0xffff);
        names[5].type = 0;
    }
}

// The three NpcName(Eng).txt names longer than 24 letters: the 0x51416b strcpy goes on through +0x1f2 (76 and 246, with
// their '\0' in the high byte) and, with the 34 of 231, through the key at +0x1f4 (which 0x512926 already wrote),
// +0x1f6 and +0x1f7 (which MuRet does not keep), the head at +0x1f8 and +0x1fa..+0x1fb (P-C352). The name of 231 has
// no '\0' in the 29 bytes MuRet keeps contiguous.
TEST_F(MonsterViewportTest, TheLongNamesOfTheTableSpillIntoTheKey)
{
    const auto path = DataFile("Local\\NpcName(Eng).txt");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto names = Client::Tables::LoadNpcNames(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(names.HasValue());
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().npcNames = Client::Monsters::NpcNameTableOf(names.Value());
        const Cell cell = FreeRow();
        const auto monster = [&cell](std::uint16_t key, std::uint8_t type)
        {
            return MonsterRecordBytes{
                .key = key, .typeLow = type, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y};
        };
        const std::array<MonsterRecordBytes, 3> records{
            {monster(0x0130, 76), monster(0x0131, 246), monster(0x0132, 231)}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Phoenix of Darkness shield");
        EXPECT_EQ(Actor(1).word1f2, 0x0064);
        EXPECT_EQ(Actor(1).key, 0x0130);
        EXPECT_EQ(Actor(1).word376, 76);
        EXPECT_EQ(Client::Monsters::ActorText(Actor(2)), "Zienna the Weapon Merchant");
        EXPECT_EQ(Actor(2).word1f2, 0x0074);
        EXPECT_EQ(Actor(2).key, 0x0131);

        // "Thompson Kenel the Weapon Merchant": " M" at +0x1f2, "er" in the key, "ch" lost, "an" in the head.
        const ActorState& merchant = Actor(3);
        EXPECT_EQ(merchant.word376, 231);
        EXPECT_EQ(Client::Monsters::ActorText(merchant), "Thompson Kenel the Weapon Mer");
        EXPECT_EQ(merchant.word1f2, 0x4d20);
        EXPECT_EQ(merchant.key, 0x7265);
        EXPECT_EQ(merchant.head, 0x6e61);
        // The key is no longer the record's: the original does not find it by key either.
        EXPECT_EQ(Client::Kernel::ActorWithKey(game->world, 0x0132), Client::Actors::CreatedActorSearch);
    }
}

// 0x4c9acb..0x4c9af7: the newly arrived NPC (237 in Lorencia, 241 in Devias) keeps alpha at 0 and does not walk; the
// tail sets class 4 (201 to 259).
TEST_F(MonsterViewportTest, ANewNpcStartsTransparentWithItsClass)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t type;
        std::uint16_t model;
    };
    for (const Case& tested : {Case{0, 237, 0x1d9}, Case{2, 241, 0x1d5}})
    {
        SCOPED_TRACE(tested.map);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0200,
                                                          .isNew = true,
                                                          .typeLow = tested.type,
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = Byte(cell.x + 3),
                                                          .targetY = cell.y,
                                                          .direction = 0x10}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, tested.model);
        EXPECT_EQ(actor.byte88, 4);
        EXPECT_EQ(actor.alpha, 0.0f);
        EXPECT_EQ(actor.movement, 0);
        EXPECT_EQ(actor.angles.z, 0.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ArrivingRecordDraws).State());
    }
    EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "\xb8\xb6\xbd\xba\xc5\xcd");
}

// In Castle Siege (30), 224 goes through the event creator 0x42d070: its rand() (0x42d40a) comes after the creation's
// 28 and before the cells, and the subtype is 10 plus the remainder.
TEST_F(MonsterViewportTest, TheCastleSiegeCreatorDrawsAfterTheCreation)
{
    ASSERT_EQ(Client::Monsters::MonsterCreationOf(224, 30).creator, 0x42d070u);
    for (const int wanted : {0, 1})
    {
        SCOPED_TRACE(wanted);
        Load(30);
        const std::uint32_t seed = Client::Tests::FirstSeedWhere(
            [wanted](CrtRandom& random)
            {
                random.Discard(ConstructionDraws);
                return random.Next() % 2 == wanted;
            });
        game->cloth.random = CrtRandom(seed);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0300, .typeLow = 224, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, 0x1fe);
        EXPECT_EQ(actor.subtype, 10 + wanted);
        EXPECT_EQ(actor.dword394, 1);
        EXPECT_EQ(actor.scale, 1.0f);
        EXPECT_EQ(actor.hands[0].model, 0xffff);
        EXPECT_EQ(actor.hands[1].model, 0xffff);
        EXPECT_EQ(actor.byte88, 4);
        EXPECT_EQ(Client::Monsters::ActorText(actor), "\xb1\xd9\xc0\xa7\xba\xb4");
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(seed, WalkingRecordDraws + 1).State());
    }
}

// 0x59e700 picks the switch or the catapult as an NPC. The 0x59daaf click talks to it at 0x568f54; the server's B2 11
// or B7 00 picks which gate or weapon to open (0x44f120 / 0x44dd10).
TEST_F(MonsterViewportTest, TheSiegeSwitchAndBothCatapultsCanBePickedForTalkAndOpenedByTheServer)
{
    constexpr std::uint16_t key = 0x1234;
    constexpr std::uint16_t gateKey = 0x5678;
    const auto file = DataFile("Local\\Skill.bmd");
    ASSERT_TRUE(file.has_value());
    const auto bytes = Client::Tests::ReadFileBytes(*file);
    ASSERT_TRUE(bytes.has_value());
    const auto skills = Client::Tables::LoadSkillTable(std::as_bytes(std::span(*bytes)));
    ASSERT_TRUE(skills.HasValue());
    struct Control
    {
        std::uint16_t npc;
        std::uint8_t weaponMode;
    };
    for (const auto [type, mode] : std::array{Control{219, 0}, Control{221, 1}, Control{222, 2}})
    {
        SCOPED_TRACE(type);
        Load(30);
        auto& runtime = game->world.Runtime();
        runtime.castleFlags.siegeActive = true;
        runtime.quest.characterClass = 0;
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = key, .typeLow = Byte(type), .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        const auto created = Monsters(records);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_NO_FATAL_FAILURE(ClickVisibleSiegeNpc(skills.Value(), key));
        ASSERT_EQ(runtime.panels.delegated.siegeGate, 0);
        ASSERT_EQ(runtime.panels.delegated.siegeWeapon, 0);
        if (mode == 0)
        {
            const std::uint8_t x = Byte(cell.x + 3);
            const std::array<MonsterRecordBytes, 1> gate{{{.key = gateKey,
                                                           .typeHigh = 1,
                                                           .typeLow = 0x15,
                                                           .effects = 0x20000,
                                                           .x = x,
                                                           .y = cell.y,
                                                           .targetX = x,
                                                           .targetY = cell.y}}};
            const auto placed = Monsters(gate);
            ASSERT_TRUE(placed.HasValue()) << placed.Error();
            ASSERT_EQ(Actor(2).key, gateKey);
        }
        const auto wire = mode == 0 ? Client::Tests::Bytes({0xc1, 7, 0xb2, 0x11, 1, 0x56, 0x78})
                                    : Client::Tests::Bytes({0xc1, 8, 0xb7, 0, 1, mode, 0x12, 0x34});
        const auto reply = Client::Protocol::CastleSiege::ReadCastleBattlePacket(Client::Tests::PacketOf(wire));
        ASSERT_TRUE(reply.HasValue());
        std::size_t extraSends = 0;
        const auto opened = Client::CastleSiege::ApplyCastleBattlePacket(
            reply.Value(), {.world = game->world,
                            .random = game->cloth.random,
                            .log = game->log,
                            .send = [&](Client::Protocol::Outgoing) { ++extraSends; }});
        EXPECT_EQ(extraSends, 0u);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_EQ(runtime.panels.delegated.siegeGate, mode == 0 ? 1 : 0);
        EXPECT_EQ(runtime.panels.delegated.siegeWeapon, mode);
        if (mode == 0)
        {
            EXPECT_EQ(runtime.castleSiege.siegeBattleUi.gateKey, gateKey);
            EXPECT_EQ(runtime.castleSiege.siegeBattleUi.gateState, 0x20000u);
        }
        else
            EXPECT_EQ(runtime.castleSiege.siegeBattleUi.weaponKey, key);
    }
}

// 0x42d513 and 0x42d5af publish the actor both with room for the 0x134 aura and with the pool full. Its creation
// (0x51d714): life 100, the key's scale (120 · 0.01f), +0x64 and +0x68 at 0, no rand().
TEST_F(MonsterViewportTest, CastleSiegePublishesTheActorsWithOneOrNoFreeArrivalSlots)
{
    struct Kind
    {
        std::uint16_t wire;
        std::uint16_t model;
    };
    constexpr std::array kinds{Kind{278, 0x1a8}, Kind{283, 0x19c}};
    constexpr std::uint16_t key = 0x0123;
    for (const auto& [wire, model] : kinds)
    {
        SCOPED_TRACE(wire);
        // One free slot is enough for 518cf0 to enter the 134/0 effect.
        Load(30);
        auto effects = game->world.Map().effects.Effects();
        for (auto& effect : effects)
            effect.alive = true;
        effects.back().alive = false;
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = key,
                                                          .typeHigh = Byte(wire >> 8),
                                                          .typeLow = Byte(wire),
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = cell.x,
                                                          .targetY = cell.y}}};
        const auto withSpace = Monsters(records);
        ASSERT_TRUE(withSpace.HasValue()) << withSpace.Error();
        ASSERT_TRUE(Actor(1).alive);
        EXPECT_EQ(Actor(1).key, key);
        EXPECT_EQ(Actor(1).type, model);
        EXPECT_TRUE(effects.back().alive);
        EXPECT_EQ(effects.back().type, 0x134);
        EXPECT_EQ(effects.back().ownerContext, &Actor(1));
        EXPECT_EQ(effects.back().disturbanceState, 120);
        EXPECT_EQ(effects.back().transform.position, Actor(1).position);
        EXPECT_EQ(effects.back().life, 100);
        EXPECT_EQ(effects.back().transform.scale, static_cast<float>(120.0 * static_cast<double>(0.01f)));
        EXPECT_EQ(effects.back().objectState.blendMesh, 0);
        EXPECT_EQ(effects.back().objectState.light, 0.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws).State());

        // The same creation with the table full completes without touching its live entries.
        Load(30);
        effects = game->world.Map().effects.Effects();
        for (auto& effect : effects)
        {
            effect.alive = true;
            effect.life = 42;
        }

        const auto applied = Monsters(records);

        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_TRUE(Actor(1).alive);
        EXPECT_EQ(Actor(1).key, key);
        EXPECT_EQ(Actor(1).type, model);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws).State());
        EXPECT_TRUE(std::ranges::all_of(effects, [](const Client::Effects::ModelEffect& effect)
                                        { return effect.alive && effect.life == 42; }));
    }
}

// 0x51443f, 163 in Devil Square (9): the case's rand() comes before the creation's and picks the .bmd, the model
// (0x199 or 0x19a, with the 0x5122d4 box) and the weapon.
TEST_F(MonsterViewportTest, TheRandomOfACaseCanGoBeforeTheCreation)
{
    for (const int wanted : {0, 1})
    {
        SCOPED_TRACE(wanted);
        Load(9);
        const std::uint32_t seed =
            Client::Tests::FirstSeedWhere([wanted](CrtRandom& random) { return random.Next() % 2 == wanted; });
        game->cloth.random = CrtRandom(seed);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{
            {{.key = 0x0400, .typeLow = 163, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& actor = Actor(1);
        EXPECT_EQ(actor.type, 0x199 + wanted);
        EXPECT_EQ(actor.scale, Real(0x3f666666));
        EXPECT_EQ(actor.hands[0].model, wanted == 0 ? 0xa25 : 0xc17);
        EXPECT_EQ(actor.hands[1].model, 0xffff);
        EXPECT_EQ(actor.boundsMaxX, 40.0f);
        EXPECT_EQ(actor.boundsTop, 120.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(seed, 1 + WalkingRecordDraws).State());
    }
}

// In Crywolf (34), 312 of 0x430c90 names three bones (0x401000); creating it again with the same key replaces them and
// the removal (0x510ff0 → 0x401040) drops them.
TEST_F(MonsterViewportTest, TheCrywolfStatueNamesItsBonesUntilItLeaves)
{
    Load(34);
    const Cell cell = FreeRow();
    const std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0500, .typeHigh = 1, .typeLow = 312 - 0x100, .x = cell.x, .y = cell.y}}};
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
    }
    const ActorState& actor = Actor(1);
    EXPECT_EQ(actor.type, 0x1b2);
    EXPECT_EQ(actor.word376, 312);
    EXPECT_EQ(actor.scale, Real(0x3f99999a));
    const auto& bones = game->world.Runtime().namedBones;
    ASSERT_EQ(bones.size(), 3u);
    EXPECT_EQ(bones[0].actor, 1u);
    EXPECT_EQ(bones[0].name, "Monster96_Top");
    EXPECT_EQ(bones[0].bone, 27u);
    EXPECT_EQ(bones[1].name, "Monster96_Center");
    EXPECT_EQ(bones[1].bone, 28u);
    EXPECT_EQ(bones[2].name, "Monster96_Bottom");
    EXPECT_EQ(bones[2].bone, 29u);

    Client::Actors::RemoveActor(game->world, 0x0500);
    EXPECT_TRUE(game->world.Runtime().namedBones.empty());
}

// Crywolf's 348 (0x4313a2): the creation turns it 180 and, with the argument's X at 62 or 183, writes 90 to +0x24. The
// 0x13 turns it again with the 0x4c9d01..0x4c9d36 step, which reads +0x420 and +0x41c without opening them: what the
// last close of 0x511290 left, four rand() & 0xff that never give 0x5a, 0x3e or 0xb7 (Q-C849), so the step is 45.
// Only with the entry opened by someone else (the count at 1) is the variable in clear, giving 0 (Y at 90) or −135 (X
// at 62).
TEST_F(MonsterViewportTest, TheCrywolfAltarReadsItsClosedCell)
{
    Load(34);
    auto created = Client::Monsters::CreateMonster(348, 100, 100, 0x0600, game->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(Actor(created.Value()).angles.z, 180.0f);
    created = Client::Monsters::CreateMonster(348, 62, 100, 0x0601, game->Resources());
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(Actor(created.Value()).angles.z, 90.0f);

    struct Case
    {
        std::uint16_t key;
        Cell cell;
        bool openY;
        bool openX;
        float angle;
    };
    for (const Case& tested :
         {Case{0x0602, {62, 100}, false, false, 90.0f}, Case{0x0602, {183, 100}, false, false, 90.0f},
          Case{0x0602, {100, 90}, false, false, 90.0f}, Case{0x0603, {100, 90}, true, false, 0.0f},
          Case{0x0604, {62, 100}, false, true, -270.0f}})
    {
        SCOPED_TRACE(tested.key);
        SCOPED_TRACE(tested.cell.x);
        const std::size_t slot = tested.key - 0x0602 + 3;
        if (tested.openY)
            Actor(slot).cellYAccesses = 1;
        if (tested.openX)
            Actor(slot).cellXAccesses = 1;
        const std::array<MonsterRecordBytes, 1> records{{{.key = tested.key,
                                                          .typeHigh = 1,
                                                          .typeLow = 348 - 0x100,
                                                          .x = tested.cell.x,
                                                          .y = tested.cell.y,
                                                          .targetX = tested.cell.x,
                                                          .targetY = tested.cell.y,
                                                          .direction = 0x30}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Actor(slot).type, 0x1b5);
        EXPECT_EQ(Actor(slot).angles.z, tested.angle);
    }
}

// 0x612d90 and 0x612270 from the creation (P-C176): in Lorencia type 3 loads Monster10.bmd into 0x15b with action 2 at
// 1.2 (0x612f4a); in Devias 19, Monster13.bmd into 0x15e with the head bone at 0x14 and its sounds (0x6133f4); the
// smith (251) Smith01.bmd into 0x1ce with its actions at 0.25; in Chaos Castle (18) 162 reads the Monster71.bmd
// textures from the Npc folder (0x612e0d) and in Castle Siege (30) 283 those of Monster75.bmd from Object31
// (0x612e2a), before cutting at its arrival (CL11). Loading draws no rand() and does not reread what is loaded.
TEST_F(MonsterViewportTest, TheCreationLoadsTheModelsOfItsCase)
{
    struct Case
    {
        std::uint8_t map;
        std::uint16_t type;
        std::uint16_t model;
        const char* folder;
    };
    const std::array<Case, 5> cases{{
        {0, 3, 0x15b, "Monster\\"},
        {2, 19, 0x15e, "Monster\\"},
        {0, 251, 0x1ce, "Npc\\"},
        {18, 162, 0x198, "Npc\\"},
        {30, 283, 0x19c, "Object31\\"},
    }};
    for (const Case& tested : cases)
    {
        SCOPED_TRACE(tested.type);
        Load(tested.map);
        const Cell cell = FreeRow();
        const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0123,
                                                          .typeHigh = Byte(tested.type >> 8),
                                                          .typeLow = Byte(tested.type),
                                                          .x = cell.x,
                                                          .y = cell.y,
                                                          .targetX = cell.x,
                                                          .targetY = cell.y}}};
        const auto applied = Monsters(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        auto& models = game->states.assets.fauna;
        const auto* model = models.Find(Client::Models::ModelId{tested.model});
        ASSERT_NE(model, nullptr);
        ASSERT_FALSE(model->meshes.empty());
        EXPECT_EQ(models.MeshTextures(Client::Models::ModelId{tested.model}).size(), model->meshes.size());
        const auto requests = models.TextureTable().Slots().ToLoad();
        ASSERT_FALSE(requests.empty());
        EXPECT_TRUE(requests.front().path.starts_with(tested.folder)) << requests.front().path;
        EXPECT_GT(game->store.loadCalls, 0);
        EXPECT_EQ(Actor(1).type, tested.model);
        if (tested.model == 0x198)
        {
            // 0x46 goes through 0x613053: actions 3 and 4 at 0.5 and 2 at 0.3; the head bone, 0x14.
            const auto* skeleton = models.SkeletonOf(Client::Models::ModelId{0x198});
            ASSERT_NE(skeleton, nullptr);
            EXPECT_EQ(skeleton->actions[3].speed, 0.5f);
            EXPECT_EQ(skeleton->actions[2].speed, Real(0x3e99999a));
            EXPECT_EQ(skeleton->headBone, 0x14);
        }
    }

    Load(2);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0124, .typeLow = 19, .x = cell.x, .y = cell.y, .targetX = cell.x, .targetY = cell.y}}};
    ASSERT_TRUE(Monsters(records).HasValue());
    const auto& devias = game->states.assets.fauna;
    EXPECT_EQ(devias.SkeletonOf(Client::Models::ModelId{0x15e})->actions[2].speed, Real(0x3e99999a));
    EXPECT_EQ(devias.SkeletonOf(Client::Models::ModelId{0x15e})->headBone, 0x14);
    EXPECT_EQ(devias.SoundActions(0x15e)[0], 0x44);
    const int loads = game->store.loadCalls;
    records[0].key = 0x0125;
    ASSERT_TRUE(Monsters(records).HasValue());
    EXPECT_EQ(game->store.loadCalls, loads);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * WalkingRecordDraws).State());

    Load(0);
    const Cell lorencia = FreeRow();
    records[0] = {
        .key = 0x0126, .typeLow = 3, .x = lorencia.x, .y = lorencia.y, .targetX = lorencia.x, .targetY = lorencia.y};
    ASSERT_TRUE(Monsters(records).HasValue());
    EXPECT_EQ(game->states.assets.fauna.SkeletonOf(Client::Models::ModelId{0x15b})->actions[2].speed, Real(0x3f99999a));
    records[0] = {
        .key = 0x0127, .typeLow = 251, .x = lorencia.x, .y = lorencia.y, .targetX = lorencia.x, .targetY = lorencia.y};
    ASSERT_TRUE(Monsters(records).HasValue());
    for (const auto& action : game->states.assets.fauna.SkeletonOf(Client::Models::ModelId{0x1ce})->actions)
        EXPECT_EQ(action.speed, 0.25f);
}

// 0x51481a: 68 asks for Monster50.bmd, which the lab's Data lacks; the original closes (Q-C23) and MuRet cuts before
// the creation, with no rand().
TEST_F(MonsterViewportTest, TheMissingMonsterFileCutsBeforeTheCreation)
{
    Load(0);
    const std::array<MonsterRecordBytes, 1> records{{{.key = 0x0700, .typeLow = 68, .x = 130, .y = 130}}};
    const auto applied = Monsters(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x51481a"), std::string::npos) << applied.Error();
    EXPECT_NE(applied.Error().find("Monster50.bmd"), std::string::npos) << applied.Error();
    EXPECT_FALSE(Actor(1).alive);
    EXPECT_EQ(game->cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c9790: 85 requests sound 0x173; 21 requests action 1 (0x4c97ff) with the actions of Monster15.bmd, which its case
// loaded into model 0x160 (0x612d90(0xe), P-C176), and sound 0x13. 0x35 (0x4c9828) sets +0x3e5 to 0x3c and action 1,
// with no sound or rand().
TEST_F(MonsterViewportTest, TheNewArrivalsAskForTheirSoundOrAction)
{
    Load(0);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{
        {{.key = 0x0800, .isNew = true, .typeLow = 85, .x = cell.x, .y = cell.y}}};
    auto applied = Monsters(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(1).type, 0x180);
    EXPECT_EQ(Actor(1).hands[1].model, 0xa15);
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x173});
    EXPECT_EQ(Actor(1).alpha, 1.0f);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ArrivingRecordDraws).State());

    records[0] = {.key = 0x0801, .isNew = true, .typeLow = 21, .x = cell.x, .y = cell.y};
    applied = Monsters(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Actor(2).type, 0x160);
    ASSERT_NE(game->states.assets.fauna.SkeletonOf(Client::Models::ModelId{0x160}), nullptr);
    EXPECT_EQ(Actor(2).animation.action, 1);
    EXPECT_EQ(Actor(2).animation.previousAction, 1);
    EXPECT_EQ(Actor(2).alpha, 1.0f);
    EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x13});
    // Loading draws no rand(): the creation and the cells, like 85.
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * ArrivingRecordDraws).State());

    ActorState arriving = Actor(2);
    arriving.word376 = 0x35;
    arriving.alpha = 0.0f;
    arriving.animation.action = 0;
    arriving.animation.previousAction = 0;
    game->world.Runtime().lastSoundId.reset();
    auto arrived = Client::Monsters::ArriveAsNew(arriving, game->Resources());
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(arriving.movementCooldown, 0x3c);
    EXPECT_EQ(arriving.animation.action, 1);
    EXPECT_EQ(arriving.animation.previousAction, 1);
    EXPECT_EQ(arriving.alpha, 1.0f);
    EXPECT_FALSE(game->world.Runtime().lastSoundId.has_value());
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 2 * ArrivingRecordDraws).State());
    arriving.word376 = 0x154;
    arrived = Client::Monsters::ArriveAsNew(arriving, game->Resources());
    ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
    EXPECT_EQ(arriving.alpha, 0.0f);
}

// 0x515f9c/515fbc and cases 5147eb..514dcc: the 618/2, 618/3 contract does not depend on 349 or on map 34.
TEST_F(MonsterViewportTest, OrdinaryMonsterCreationAttachesBothOwnedBeamsInNativeOrder)
{
    for (const std::int32_t type : {53, 57, 58, 59, 60, 61, 62, 63, 82, 83, 300})
    {
        SCOPED_TRACE(type);
        Load(0);
        const Cell cell = FreeRow();
        const auto created = Client::Monsters::CreateMonster(type, cell.x, cell.y, 100, game->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ActorState& actor = Actor(created.Value());
        EXPECT_EQ(actor.word376, type);
        auto& joints = game->world.DrawTail().joints;
        for (std::size_t index = 0; index < 2; ++index)
        {
            EXPECT_TRUE(joints[index].alive);
            EXPECT_EQ(joints[index].type, 0x618);
            EXPECT_EQ(joints[index].subtype, static_cast<int>(index) + 2);
            EXPECT_EQ(joints[index].ownerContext, &actor);
            EXPECT_EQ(joints[index].ownerKind, Client::Kernel::JointOwnerKind::Actor);
            EXPECT_EQ(joints[index].width, 30.0f);
            EXPECT_EQ(joints[index].life, 999999999);
        }
        EXPECT_FALSE(joints[2].alive);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ConstructionDraws).State());
    }
}

// 0x51656e..0x5165d1: NPC 257 (a 0x20a) drops the 0x60f/0x2a of width 15 owned by it, born at (0, −15, 0) rotated by
// its angles beyond its position. No rand(): with the joints full, the RNG stays the same.
TEST_F(MonsterViewportTest, TheNpc257CarriesItsSpinningSparkle)
{
    std::optional<std::uint32_t> drawn;
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Load(0);
        auto& joints = game->world.DrawTail().joints;
        if (full)
            for (auto& joint : joints)
                joint.alive = true;
        const Cell cell = FreeRow();
        const auto created = Client::Monsters::CreateMonster(257, cell.x, cell.y, 100, game->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ActorState& actor = Actor(created.Value());
        if (!full)
        {
            const Client::Kernel::JointState& joint = joints[0];
            EXPECT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x60f);
            EXPECT_EQ(joint.subtype, 0x2a);
            EXPECT_EQ(joint.ownerContext, &actor);
            EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::Actor);
            EXPECT_EQ(joint.width, 15.0f);
            EXPECT_EQ(joint.life, 100);
            const auto moved = Client::Math::OriginalVectorRotate({0.0f, -15.0f, 0.0f},
                                                                  Client::Math::OriginalAngleMatrix(actor.angles));
            const glm::vec3 path{static_cast<float>(static_cast<double>(moved.x) + actor.position.x),
                                 static_cast<float>(static_cast<double>(moved.y) + actor.position.y),
                                 static_cast<float>(static_cast<double>(moved.z) + actor.position.z)};
            EXPECT_EQ(joint.pathIncrement, path);
            EXPECT_EQ(joint.targetPosition, actor.position);
            EXPECT_FALSE(joints[1].alive);
            drawn = game->cloth.random.State();
        }
        else
            EXPECT_EQ(game->cloth.random.State(), drawn);
    }
}

TEST_F(MonsterViewportTest, OrdinaryCreationFinishesItsTailWithOneOrNoJointSlots)
{
    for (const std::size_t free : {std::size_t{0}, std::size_t{1}})
    {
        SCOPED_TRACE(free);
        Load(0);
        auto& joints = game->world.DrawTail().joints;
        for (std::size_t index = free; index < joints.size(); ++index)
            joints[index].alive = true;
        const auto created = Client::Monsters::CreateMonster(53, 120, 120, 100, game->Resources());
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(Actor(created.Value()).word376, 53);
        if (free == 1)
        {
            EXPECT_EQ(joints[0].type, 0x618);
            EXPECT_EQ(joints[0].subtype, 2);
        }
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, ConstructionDraws).State());
    }
}

// 0x4c9883..4c9aca: stage 3 only publishes alpha 1; the others complete 4×615/7, 3×5f6/1, 4×614/17.
TEST_F(MonsterViewportTest, CrywolfArrivalsUseTheTourStageAndPreserveTheJointWidthDraws)
{
    for (const std::uint16_t type :
         {std::uint16_t{0x154}, std::uint16_t{0x155}, std::uint16_t{0x158}, std::uint16_t{0x159}})
    {
        SCOPED_TRACE(type);
        game = std::make_unique<Game>();
        ActorState& actor = Actor(1);
        actor.word376 = type;
        actor.position = {100, 200, 300};
        game->world.Runtime().events.cameraTour.stage = 3;
        ASSERT_TRUE(Client::Monsters::ArriveAsNew(actor, game->Resources()).HasValue());
        EXPECT_EQ(actor.alpha, 1.0f);
        EXPECT_FALSE(game->world.DrawTail().joints[0].alive);
        EXPECT_FALSE(game->world.Particles().Any());
        EXPECT_EQ(game->cloth.random.State(), Seed);
        game->world.Runtime().events.cameraTour.stage = 2;
        ASSERT_TRUE(Client::Monsters::ArriveAsNew(actor, game->Resources()).HasValue());
        EXPECT_EQ(actor.alpha, 0.0f);
        const auto& joints = game->world.DrawTail().joints;
        CrtRandom widths(Seed);
        constexpr std::array<int, 4> bases{60, 50, 50, 60};
        for (std::size_t index = 0; index < 4; ++index)
        {
            EXPECT_EQ(joints[index].type, 0x615);
            EXPECT_EQ(joints[index].subtype, 7);
            EXPECT_EQ(joints[index].width, static_cast<float>(widths.Next() % 10 + bases[index]));
            widths.Discard(2); // 0x531b0c..0x531b57: the 615/7 constructor's two interleaved draws.
            EXPECT_EQ(joints[index].ownerContext, nullptr);
            EXPECT_EQ(joints[index].targetPosition, (glm::vec3{120, 220, 300}));
            EXPECT_EQ(joints[index + 4].type, 0x614);
            EXPECT_EQ(joints[index + 4].subtype, 17);
            EXPECT_EQ(joints[index + 4].width, 10.0f);
            EXPECT_EQ(joints[index + 4].targetPosition, (glm::vec3{100, 200, 420}));
            EXPECT_EQ(joints[index + 4].ownerContext, nullptr);
        }
        EXPECT_FALSE(joints[8].alive);
        const auto particles = game->world.Particles().All();
        EXPECT_EQ(particles[0].type, 0x5f6);
        EXPECT_EQ(particles[2].subtype, 1);
        EXPECT_FALSE(particles[3].alive);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, 34).State());
    }
}

TEST_F(MonsterViewportTest, CrywolfArrivalCompletesWithPartialAndFullEffectPools)
{
    struct Capacity
    {
        std::size_t joints;
        std::size_t particles;
        int draws;
    };
    for (const Capacity capacity : {Capacity{8, 3, 34}, Capacity{4, 3, 30}, Capacity{1, 3, 24}, Capacity{0, 3, 22},
                                    Capacity{8, 0, 16}, Capacity{0, 0, 4}})
    {
        SCOPED_TRACE(capacity.joints);
        SCOPED_TRACE(capacity.particles);
        game = std::make_unique<Game>();
        Actor(1).word376 = 0x154;
        auto& joints = game->world.DrawTail().joints;
        for (std::size_t index = capacity.joints; index < joints.size(); ++index)
            joints[index].alive = true;
        auto particles = game->world.Particles().All();
        for (std::size_t index = capacity.particles; index < particles.size(); ++index)
            particles[index].alive = true;
        const auto arrived = Client::Monsters::ArriveAsNew(Actor(1), game->Resources());
        ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
        EXPECT_EQ(Actor(1).alpha, 0.0f);
        EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, capacity.draws).State());
    }
}

// 0x4c9ed2..0x4c9f44 and 0x4e0b96..0x4e0c10: in Castle Siege, the gate (+0x376 at 0x115, the 0x19b with the 0x42d7f0
// box) with +0x78 at 0 cuts at 0x4c9f0f (the 0x13) or at 0x4e0bd1 (the 0x68); with another state it goes on.
TEST_F(MonsterViewportTest, TheCastleSiegeGateAppliesItsActionWhenItIsOpenOrUnmarked)
{
    Load(30);
    const Cell cell = FreeRow();
    std::array<MonsterRecordBytes, 1> records{{{.key = 0x0900,
                                                .typeHigh = 1,
                                                .typeLow = 0x15,
                                                .x = cell.x,
                                                .y = cell.y,
                                                .targetX = cell.x,
                                                .targetY = cell.y}}};
    auto applied = Monsters(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& gate = Actor(1);
    EXPECT_EQ(gate.type, 0x19b);
    EXPECT_EQ(gate.boundsMin, (glm::vec3{-140.0f, -140.0f, 0.0f}));
    EXPECT_EQ(gate.boundsTop, 300.0f);
    EXPECT_EQ(gate.attackCount, 0);
    EXPECT_EQ(gate.byte92, 0);

    records[0].effects = 1;
    applied = Monsters(records);
    EXPECT_TRUE(applied.HasValue()) << applied.Error();

    const std::array<ObjectRecordBytes, 1> objects{
        {{.kind = 2, .classOrTypeHigh = 1, .typeLow = 0x15, .key = 0x0901, .x = cell.x, .y = cell.y}}};
    applied = Objects(objects);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
}

// 0x4ca0b0 with 0x517400: 0x98 and 0x9e (level type − 0x97, the 7 with subtype 1), +0x88 at 1, the PK from the
// direction and, at 6 or more, the level at 1. The name is the empty string at 0x6e57a4, without the owner.
TEST_F(MonsterViewportTest, TheSummonsTakeTheirLevelFromTheType)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<SummonRecordBytes, 2> records{{{.key = 0x0a00,
                                                     .type = 0x98,
                                                     .x = cell.x,
                                                     .y = cell.y,
                                                     .targetX = cell.x,
                                                     .targetY = cell.y,
                                                     .directionAndPk = 0x33,
                                                     .owner = "Carpincho"},
                                                    {.key = 0x0a01,
                                                     .type = 0x9e,
                                                     .x = cell.x,
                                                     .y = cell.y,
                                                     .targetX = cell.x,
                                                     .targetY = cell.y,
                                                     .directionAndPk = 0x16,
                                                     .owner = "Carpincho"}}};
    const auto applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    const ActorState& first = Actor(1);
    EXPECT_EQ(first.type, 0xc5);
    EXPECT_EQ(first.word376, 0x98);
    EXPECT_EQ(first.level, 1);
    EXPECT_EQ(first.subtype, 0);
    EXPECT_EQ(first.byte88, 1);
    EXPECT_EQ(first.pk, 3);
    EXPECT_EQ(first.angles.z, 90.0f);
    EXPECT_EQ(first.dword394, 1);
    EXPECT_EQ(first.dword64, -1);
    EXPECT_EQ(first.hands[0].model, 0xffff);
    EXPECT_EQ(first.animation.frame, 10.0f);
    EXPECT_EQ(Client::Monsters::ActorText(first), "");

    const ActorState& last = Actor(2);
    EXPECT_EQ(last.word376, 0x9e);
    EXPECT_EQ(last.subtype, 1);
    EXPECT_EQ(last.pk, 6);
    EXPECT_EQ(last.level, 1);
    EXPECT_EQ(game->cloth.random.State(), AfterDraws(Seed, WalkingRecordDraws + WalkingRecordDraws).State());
}

// 0x51746e..0x517555: the arriving summon (0x98..0x9e), after the name and before the cells, drops four 0x615/1 with no
// owner from its position, of width rand() % 10 plus 60, 50, 50 and 60, each with its creation's two rand() (x and y
// ± 5, 0x531b1d and 0x531b37), 800 higher and with the tail off. With the pool full, only the four width draws. Then
// it goes on like the other new arrivals (the 0xec circle and the 0x5d5/2 ray, 0x4ca5f6 and 0x4ca611, no rand()) and,
// being 0x98..0x9e, goes back to frame 0 and sounds 0x61 (0x4ca616..0x4ca642).
TEST_F(MonsterViewportTest, ANewSummonDropsFourBoltsAfterItsName)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Load(0);
        auto& joints = game->world.DrawTail().joints;
        for (auto& joint : joints)
            joint.alive = full;
        const std::array<SummonRecordBytes, 1> records{
            {{.key = 0x0b00, .isNew = true, .type = 0x9a, .x = 130, .y = 130}}};
        const auto applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const ActorState& summon = Actor(1);
        EXPECT_TRUE(summon.alive);
        EXPECT_EQ(summon.level, 3);
        EXPECT_EQ(summon.animation.frame, 0.0f);
        EXPECT_EQ(game->world.Runtime().lastSoundId, Client::Audio::SoundId{0x61});
        const auto effects = std::as_const(game->world.Map().effects).Effects();
        const auto circle = std::ranges::find_if(effects, [](const Client::Effects::ModelEffect& effect)
                                                 { return effect.alive && effect.type == 0xec; });
        ASSERT_NE(circle, effects.end());
        EXPECT_EQ(circle->ownerContext, &summon);
        constexpr std::array<double, 4> Bases{60.0, 50.0, 50.0, 60.0};
        auto expected = AfterDraws(Seed, ConstructionDraws);
        std::size_t live = 0;
        for (const double base : Bases)
        {
            SCOPED_TRACE(base);
            const auto width = static_cast<float>(static_cast<double>(expected.Next() % 10) + base);
            if (full)
                continue;
            const float x = static_cast<float>(static_cast<double>(expected.Next() % 10 - 5) +
                                               static_cast<double>(summon.position.x));
            const float y = static_cast<float>(static_cast<double>(expected.Next() % 10 - 5) +
                                               static_cast<double>(summon.position.y));
            while (live < joints.size() && !joints[live].alive)
                ++live;
            ASSERT_LT(live, joints.size());
            const auto& bolt = joints[live++];
            EXPECT_EQ(bolt.type, 0x615);
            EXPECT_EQ(bolt.subtype, 1);
            EXPECT_EQ(bolt.width, width);
            EXPECT_EQ(bolt.ownerContext, nullptr);
            EXPECT_FALSE(bolt.tailEnabled);
            EXPECT_EQ(bolt.position,
                      glm::vec3(x, y, static_cast<float>(static_cast<double>(summon.position.z) + 800.0)));
            EXPECT_EQ(bolt.targetPosition, summon.position);
            EXPECT_EQ(bolt.pathIncrement, bolt.position);
            EXPECT_EQ(bolt.angles, glm::vec3(0.0f));
        }
        expected.Discard(CellDraws);
        EXPECT_EQ(game->cloth.random.State(), expected.State());
    }
}

// 0x4ca516..0x4ca5bf: outside 0x98..0x9e, the name is the owner's (ten bytes), Text 485 and the monster's; without
// Text.bmd it cuts at 0x4ca570. The strcats go on through +0x1f2 and the key at +0x1f4 like the original (Q-C847). The
// new arrival does not walk: ArriveAsNew (alpha at 0), the 0xec circle (0x4ca5f6) and the 0x5d5/2 ray (0x4ca611) owned
// by it, no rand() and no 0x61 sound, which belongs to 0x98..0x9e.
TEST_F(MonsterViewportTest, TheOtherSummonsTakeTheOwnersName)
{
    Load(0);
    const Cell cell = FreeRow();
    std::array<SummonRecordBytes, 1> records{{{.key = 0x0c00,
                                               .type = 3,
                                               .x = cell.x,
                                               .y = cell.y,
                                               .targetX = cell.x,
                                               .targetY = cell.y,
                                               .directionAndPk = 0x10,
                                               .owner = "Carpincho"}}};
    auto applied = Summons(records);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4ca570"), std::string::npos) << applied.Error();

    const Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{485, " con su "}});
    game->texts = &texts;
    applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Carpincho con su \xb0\xc5\xb9\xcc");
    EXPECT_EQ(Actor(1).byte88, 1);

    // Ten letters, " con su " and the nine of case 20 (0x515949): 27, with the '\0' in the key's low byte. The WORD at
    // +0x1f2 holds the last two.
    records[0].key = 0x0c02;
    records[0].owner = "Carpinchos";
    records[0].type = 20;
    applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& spilled = Actor(2);
    EXPECT_EQ(Client::Monsters::ActorText(spilled), "Carpinchos con su \xbc\xb3\xc0\xce \xb4\xeb\xc0\xe5");
    EXPECT_EQ(spilled.word1f2, 0xe5c0);
    EXPECT_EQ(spilled.key, 0x0c00);

    records[0] = {.key = 0x0c01, .isNew = true, .type = 3, .x = cell.x, .y = cell.y, .owner = "Carpincho"};
    const std::optional<Client::Audio::SoundId> lastSound = game->world.Runtime().lastSoundId;
    CrtRandom expected = game->cloth.random;
    expected.Discard(ArrivingRecordDraws);
    applied = Summons(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& arrived = Actor(3);
    EXPECT_EQ(arrived.alpha, 0.0f);
    EXPECT_EQ(game->cloth.random.State(), expected.State());
    const auto effects = std::as_const(game->world.Map().effects).Effects();
    const auto circle = std::ranges::find_if(effects, [](const Client::Effects::ModelEffect& effect)
                                             { return effect.alive && effect.type == 0xec; });
    ASSERT_NE(circle, effects.end());
    EXPECT_EQ(circle->subtype, 0);
    EXPECT_EQ(circle->ownerContext, &arrived);
    EXPECT_EQ(circle->life, 30);
    EXPECT_EQ(circle->transform.position, arrived.position);
    const auto& particles = std::as_const(game->world.Particles()).All();
    const auto bolt = std::ranges::find_if(particles, [](const Client::Particles::Particle& particle)
                                           { return particle.alive && particle.type == 0x5d5; });
    ASSERT_NE(bolt, particles.end());
    EXPECT_EQ(bolt->subtype, 2);
    EXPECT_EQ(bolt->typedOwner, (Client::Particles::ParticleOwner{Client::Particles::ParticleOwnerKind::Actor, 3}));
    EXPECT_EQ(game->world.Runtime().lastSoundId, lastSound);
}

// 0x51743f..0x517466 with NpcName(Eng).txt loaded: the tail leaves "Magic stone of %s " for 152 to 157 (0x98..0x9d) and
// "Shadow Knight" for 158 (0x9e), and wsprintfA puts the owner in the "%s". The local's 24 bytes go to +0x1d9: under
// 24 letters, with their '\0'; over, the first 24 without it, and the name goes on at +0x1f1. The owners are made up.
TEST_F(MonsterViewportTest, TheMagicStonesFormatTheOwnersName)
{
    const auto path = DataFile("Local\\NpcName(Eng).txt");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto names = Client::Tables::LoadNpcNames(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(names.HasValue());
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        game->world.Runtime().npcNames = Client::Monsters::NpcNameTableOf(names.Value());
        const Cell cell = FreeRow();
        const auto summon = [&cell](std::uint16_t key, std::uint8_t type, std::string_view owner)
        {
            return SummonRecordBytes{.key = key,
                                     .type = type,
                                     .x = cell.x,
                                     .y = cell.y,
                                     .targetX = cell.x,
                                     .targetY = cell.y,
                                     .directionAndPk = 0x10,
                                     .owner = owner};
        };
        const std::array<SummonRecordBytes, 3> records{
            {summon(0x0e00, 0x98, "Yacare"), summon(0x0e01, 0x9d, "Carpinchos"), summon(0x0e02, 0x9e, "Yacare")}};
        const auto applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();

        EXPECT_EQ(Actor(1).word376, 0x98);
        EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Magic stone of Yacare ");
        // "Magic stone of Carpinchos " has 26: the first 24, with no '\0'; +0x1f1 stays at 0 and ends the string.
        EXPECT_EQ(Client::Monsters::ActorText(Actor(2)), "Magic stone of Carpincho");
        EXPECT_EQ(Actor(2).nameSpill[13], std::byte{0});
        EXPECT_EQ(Actor(2).word1f2, 0xffff);
        EXPECT_EQ(Actor(2).key, 0x0e01);
        EXPECT_EQ(Actor(2).level, 6);
        EXPECT_EQ(Client::Monsters::ActorText(Actor(3)), "Shadow Knight");
        EXPECT_EQ(Actor(3).subtype, 1);
    }
}

// 0x51743f: wsprintfA with another specifier, or a second "%s", reads from the stack arguments 0x517400 does not pass,
// and a ten-letter owner with no '\0' after fewer than 14 bytes goes on into the 1F record within the 24 bytes: they
// cut (P-C356). "%%" gives '%'. The table names and the owners are made up.
TEST_F(MonsterViewportTest, TheSummonFormatsThatReadMoreCut)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}})
    {
        SCOPED_TRACE(map);
        Load(map);
        auto& names = game->world.Runtime().npcNames;
        const auto put = [&names](std::size_t at, std::uint16_t type, std::string_view name)
        {
            names[at].type = type;
            names[at].name = {};
            std::ranges::copy(name, names[at].name.begin());
        };
        put(0, 0x98, "Piedra 100%% de %s");
        put(1, 0x99, "Piedra %d");
        put(2, 0x9a, "%s y %s");
        put(3, 0x9b, "%s");
        put(4, 0x9c, "Piedra de mago de %s");
        const Cell cell = FreeRow();
        std::array<SummonRecordBytes, 1> records{{{.key = 0x0f00,
                                                   .type = 0x98,
                                                   .x = cell.x,
                                                   .y = cell.y,
                                                   .targetX = cell.x,
                                                   .targetY = cell.y,
                                                   .owner = "Yacare"}}};
        auto applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Piedra 100% de Yacare");

        for (const std::uint16_t type : {std::uint16_t{0x99}, std::uint16_t{0x9a}})
        {
            records[0].type = type;
            applied = Summons(records);
            ASSERT_FALSE(applied.HasValue());
            EXPECT_NE(applied.Error().find("0x51743f"), std::string::npos) << applied.Error();
            EXPECT_NE(applied.Error().find("P-C356"), std::string::npos) << applied.Error();
        }

        // No '\0' in the owner's ten bytes: after a bare "%s", it cuts; after 18 bytes, the 24 are known.
        records[0].owner = "Carpinchos";
        records[0].type = 0x9b;
        applied = Summons(records);
        ASSERT_FALSE(applied.HasValue());
        EXPECT_NE(applied.Error().find("P-C356"), std::string::npos) << applied.Error();
        records[0].type = 0x9c;
        applied = Summons(records);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(Client::Monsters::ActorText(Actor(1)), "Piedra de mago de Carpin");
    }
}

// 0x4e0980: with 1, a player (0x20a) with the class from +1, +0x88 at 0x80, +0x428 at 0x96, the angle rand() % 360 and
// the name "   "; with 2, the monster of the type from +1 and +2 with the same angle; with 0, nothing.
TEST_F(MonsterViewportTest, TheObjectsArePlayersOrMonstersByTheirKind)
{
    Load(0);
    const Cell cell = FreeRow();
    const std::array<ObjectRecordBytes, 3> records{{
        {.kind = 0, .typeLow = 3, .key = 0x0d00, .x = cell.x, .y = cell.y},
        {.kind = 1, .classOrTypeHigh = 0x20, .key = 0x0d01, .x = cell.x, .y = cell.y},
        {.kind = 2, .typeLow = 3, .key = 0x0d02, .x = cell.x, .y = cell.y},
    }};
    const auto applied = Objects(records);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();

    CrtRandom expected = AfterDraws(Seed, ArrivingRecordDraws);
    const ActorState& player = Actor(1);
    EXPECT_EQ(player.key, 0x0d01);
    EXPECT_EQ(player.type, PlayerSkeletonModel);
    EXPECT_EQ(player.characterClass, 1);
    EXPECT_EQ(player.byte88, 0x80);
    EXPECT_EQ(player.dword428, 0x96);
    EXPECT_EQ(player.angles.z, static_cast<float>(expected.Next() % 360));
    EXPECT_EQ(Client::Monsters::ActorText(player), "   ");
    EXPECT_EQ(player.byte1e3, 0);

    expected.Discard(ArrivingRecordDraws);
    const ActorState& monster = Actor(2);
    EXPECT_EQ(monster.key, 0x0d02);
    EXPECT_EQ(monster.type, 0x15b);
    EXPECT_EQ(monster.byte88, 2);
    EXPECT_EQ(monster.dword428, 0x96);
    EXPECT_EQ(monster.angles.z, static_cast<float>(expected.Next() % 360));
    EXPECT_FALSE(Actor(3).alive);
    EXPECT_EQ(game->cloth.random.State(), expected.State());
}
} // namespace
