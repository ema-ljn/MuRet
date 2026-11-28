#include "Npc/NpcTalk.h"

#include "Models/LogicalAccess.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Tables/TextTable.h"
#include "Pathfinding/Path.h"
#include "Pathfinding/PathCell.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Skeleton.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovement.h"
#include "Session/HeroMovementHooks.h"
#include "Movement/GameMovementState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Quests/QuestState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Talking to an NPC (CL9 milestone 2): MoveHero's click on the chosen NPC (0x59da75..0x59dc59) and the arrival
// (0x5650c0 with +0x379 at 2, 0x5674c8..0x5695b3) with the C1 30. Texts and keys made up; the world in Lorencia and,
// in the all-maps cases, also in Devias (outside Crywolf, 0x5674d2 and 0x567b00, the map changes nothing). The terrain
// has no walls, the same in both.
namespace
{
const Client::Session::HeroMovementHooks MovementHooks{};
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Movement::HeroMovementContext;

constexpr std::size_t NpcSlot = 7;
constexpr std::uint16_t TalkingWord = 0xf0;
constexpr std::uint16_t NpcKey = 0x8123;
constexpr std::size_t LevelText = 663;
constexpr std::size_t FlagText = 1830;
// The own record (0x40b4d0) and a C3 (the padding and the serial).
constexpr int OwnRecordRandoms = 3848;
constexpr int C3Randoms = 2;
// What the step draws before the click (the two closes of the counter at 0x6c8c10 and the click's record) and after
// the D3 (the trade and the NPC shop of 0x56b397.., the own record and Skill.bmd of the action's prefix).
constexpr int BeforeTheClickRandoms = 8 + OwnRecordRandoms;
constexpr int AfterTheWalkRandoms = 2 + OwnRecordRandoms + 60;
// Lorencia and Devias.
constexpr std::array<std::uint8_t, 2> TalkMaps{0, 2};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class TypedLetters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class TalkingHero
{
public:
    explicit TalkingHero(std::uint8_t map = 0)
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        // The quests already have the class: no A0.
        runtime.quest.characterClass = 0;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        terrain.mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        ActorState& hero = Hero();
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.cellX = hero.targetX = 182;
        hero.cellY = hero.targetY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.hands[1].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.alpha = 1.0f;
        hero.byte88 = 1;
        hero.baseMovementSpeed = 10;
        ActorState& npc = Npc();
        npc.alive = true;
        npc.type = 0xf9;
        npc.position = {18550.0f, 11550.0f, 0.0f};
        npc.angles.z = 90.0f;
        npc.word376 = TalkingWord;
        npc.key = NpcKey;
        skeleton.actions.resize(205);
        for (auto& action : skeleton.actions)
            action.keyCount = 10;
    }

    ActorState& Hero()
    {
        return world.Actors()[0];
    }
    ActorState& Npc()
    {
        return world.Actors()[NpcSlot];
    }

    HeroMovementContext Context()
    {
        HeroMovementContext context{.world = world, .random = random, .playerSkeleton = &skeleton, .skills = &skills};
        context.hooks = &MovementHooks;
        context.terrain = &terrain;
        context.sendWalk = [this](std::span<const Client::Protocol::Movement::Cell> /*path*/,
                                  Client::Protocol::Movement::Cell target,
                                  float /*angle*/) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(std::format("D3 {} {}", target.x, target.y));
            randomAtTheWalk = random;
            return {};
        };
        context.sendTalk = [this](std::int16_t key)
        { events.push_back(std::format("30 {:04x}", static_cast<std::uint16_t>(key))); };
        context.requestQuestList = [this]() { events.push_back("A0"); };
        context.sendWindowClose = [this]() { events.push_back("31"); };
        context.texts = &texts;
        context.measure = &letters;
        return context;
    }

    // A click with the chosen NPC (0x6c8e2c) and the MouseUpdateTime window expired.
    Client::Core::Expected<void, std::string> Click()
    {
        world.Runtime().selectedTargets[1] = static_cast<std::int32_t>(NpcSlot);
        world.Runtime().mouse.updateTime = 6;
        auto context = Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget =
            [this]() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        {
            events.emplace_back("pick");
            return std::optional<Client::Pathfinding::PathCell>{};
        };
        return Client::Movement::StepHeroMovement(context);
    }

    // The arrival with the NPC in 0x6c8e54 and the hero walking (+0x378 at 1).
    Client::Core::Expected<void, std::string> Arrive()
    {
        world.Runtime().chosenNpc.index = static_cast<std::int32_t>(NpcSlot);
        Hero().movement = 1;
        return Client::Npc::TalkToTheNpc(Hero(), Context());
    }

    bool Said(std::string_view text, std::int32_t type)
    {
        return std::ranges::any_of(world.Runtime().chat.area.rows, [&](const Client::Ui::MessageAreaRow& row)
                                   { return row.text == text && row.type == type; });
    }

    WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    std::optional<CrtRandom> randomAtTheWalk;
    Client::Models::Skeleton skeleton;
    Client::Tables::SkillTable skills{std::vector<std::byte>(Client::Tables::SkillTableSize)};
    TypedLetters letters;
    Client::Tables::TextTable texts =
        Client::Tests::MakeTextTable({{LevelText, "hace falta el nivel %d"}, {FlagText, "no con eso puesto"}});
    std::vector<std::string> events;
};

// What 0x4f2250 draws when stopping the fixture's hero, from `start`, on a twin.
CrtRandom AfterTheStop(const CrtRandom& start)
{
    const auto twin = std::make_unique<TalkingHero>();
    twin->random = start;
    const Client::Actors::ActorStopContext stop{twin->world.Runtime(),
                                                Client::Kernel::SceneId::Game,
                                                twin->skeleton,
                                                nullptr,
                                                twin->random,
                                                &twin->terrain.attributes->cells,
                                                {}};
    EXPECT_TRUE(Client::Actors::SetActorStop(twin->Hero(), stop).HasValue());
    return twin->random;
}

// 0x59da75..0x59dc45: the click walks to the NPC's cell, with +0x379 at 2 and the NPC, its type and its angle stored;
// the NPC shop draws its rand() before the hero's cells.
TEST(NpcTalkTest, AClickOnTheChosenNpcWalksToItsCell)
{
    for (const std::uint8_t map : TalkMaps)
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>(map);
        const CrtRandom start = tested->random;
        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
        ASSERT_TRUE(tested->randomAtTheWalk.has_value());
        EXPECT_EQ(tested->randomAtTheWalk->State(), Advanced(start, BeforeTheClickRandoms + 1 + 8).State());
        EXPECT_EQ(tested->random.State(), Advanced(start, BeforeTheClickRandoms + 1 + 8 + AfterTheWalkRandoms).State());
        const auto& movement = tested->world.Runtime().movement;
        const Client::Kernel::MouseState& mouse = tested->world.Runtime().mouse;
        EXPECT_EQ(tested->Hero().movementType, Client::Movement::NpcTalkMovement);
        EXPECT_EQ(tested->Hero().movement, 1);
        EXPECT_EQ(movement.attackTarget, 7);
        const Client::Kernel::ChosenNpc& chosen = tested->world.Runtime().chosenNpc;
        EXPECT_EQ(chosen.index, 7);
        EXPECT_EQ(chosen.type, 0xf9);
        EXPECT_EQ(chosen.angle, 0x42b40000u);
        EXPECT_EQ(movement.targetX, 185u);
        EXPECT_EQ(movement.targetY, 115u);
        // 0x59dc48: goes on at 0x59e248, which adds to MouseUpdateTime.
        EXPECT_EQ(mouse.updateTime, 1);
    }
}

// 0x59dab8: with bit 0x10000000 of +0x7c the click walks anyway, but +0x379 stays as it was.
TEST(NpcTalkTest, TheBlockingFlagWalksWithoutTheTalk)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Hero().flags7c = Client::Movement::NpcTalkBlockedFlag;
    const auto clicked = tested->Click();
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
    EXPECT_EQ(tested->Hero().movementType, 0);
    EXPECT_EQ(tested->world.Runtime().chosenNpc.index, 7);
}

// 0x59da82..0x59daa9: with the NPC shop or the warehouse open, the click goes on to the ground.
TEST(NpcTalkTest, TheShopOrTheWarehouseSendTheClickToTheGround)
{
    for (const bool shop : {true, false})
    {
        SCOPED_TRACE(shop);
        const auto tested = std::make_unique<TalkingHero>();
        auto& panels = tested->world.Runtime().panels;
        (shop ? panels.npcShop : panels.warehouse) = 1;
        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"pick"}));
        EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
        EXPECT_EQ(tested->world.Runtime().movement.attackTarget, -1);
    }
}

// 0x59dc4d: without a path (the NPC on the hero's cell), the arrival is in place and talks to it.
TEST(NpcTalkTest, WithoutAPathTheHeroTalksInPlace)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().position = tested->Hero().position;
    const auto clicked = tested->Click();
    ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
    EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
    EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
    EXPECT_EQ(tested->world.Runtime().mouse.updateTimeMax, 12);
    EXPECT_EQ(tested->world.Runtime().mouse.updateTime, 1);
    EXPECT_EQ(tested->Hero().movementType, Client::Movement::NpcTalkMovement);
}

// 0x59b2f7: the hero walks to the NPC and, when the path ends, 0x5650c0 with +0x379 at 2 talks to it.
TEST(NpcTalkTest, TheHeroTalksWhenItArrives)
{
    for (const std::uint8_t map : TalkMaps)
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>(map);
        ASSERT_TRUE(tested->Click().HasValue());
        ActorState& hero = tested->Hero();
        ASSERT_EQ(hero.movement, 1);
        constexpr int StepLimit = 100;
        for (int step = 0; step < StepLimit && hero.movement != 0; ++step)
        {
            const auto stepped = Client::Movement::StepHeroMovement(tested->Context());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115", "30 8123"}));
        EXPECT_EQ(hero.movement, 0);
        EXPECT_EQ(hero.cellX, 185);
        EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
        EXPECT_EQ(tested->world.Runtime().mouse.updateTimeMax, 12);
    }
}

// 0x59dc32: an NPC outside the map cuts.
TEST(NpcTalkTest, AnNpcOutsideTheMapIsCut)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().position.x = 25700.0f;
    const auto clicked = tested->Click();
    ASSERT_FALSE(clicked.HasValue());
    EXPECT_NE(clicked.Error().find("0x59dc32"), std::string::npos) << clicked.Error();
}

// 0x4eac0a..0x4eac41 from 0x59dc32: with Crywolf active, the altar just chosen (0x59db1f) sets the A* flag, which
// zeroes its cell's word (0x4eae0d) even if it is a wall: the path reaches the altar's cell. With stage 2 the goal
// stays and the click goes to the nearest.
TEST(NpcTalkTest, TheWalkToACrywolfAltarClearsItsCell)
{
    for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
    {
        SCOPED_TRACE(stage);
        const auto tested = std::make_unique<TalkingHero>();
        tested->world.Runtime().map = 34;
        tested->world.Runtime().crywolf.stage = stage;
        tested->Npc().type = 0x205;
        auto& walls = tested->terrain.attributes->cells;
        const std::size_t altar = Client::Data::Terrain::CellIndex(185, 115);
        walls[altar] = 0x06;
        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 185 115"}));
        const auto& path = tested->Hero().path;
        ASSERT_GE(path.count, 2);
        const Client::Pathfinding::PathCell last = path.cells[path.count - 1U];
        if (stage == 2)
        {
            EXPECT_EQ(walls[altar], 0x06);
            // The nearest of 0x4eb0f4..0x4eb3fc: the first with heuristic 4 is the upper diagonal.
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{184, 114}));
        }
        else
        {
            EXPECT_EQ(walls[altar], 0);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{185, 115}));
        }
    }
}

// 0x59e21f → 0x4eac00 (Q-C1351): the ground click also carries the flag if 0x6c8e54 still has an altar (a path to an
// altar that the click cut). On map 34 with stage 4 the click's wall cell goes to 0 and the path arrives; in Lorencia
// and Devias, the nearest.
TEST(NpcTalkTest, AGroundClickWithAChosenAltarAlsoClearsItsCell)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>();
        auto& runtime = tested->world.Runtime();
        runtime.map = map;
        runtime.crywolf.stage = 4;
        runtime.chosenNpc.index = static_cast<std::int32_t>(NpcSlot);
        tested->Npc().type = 0x205;
        auto& walls = tested->terrain.attributes->cells;
        const std::size_t clicked = Client::Data::Terrain::CellIndex(186, 115);
        walls[clicked] = 0x04;
        runtime.mouse.updateTime = 6;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        context.pickTarget = []() -> Client::Core::Expected<std::optional<Client::Pathfinding::PathCell>, std::string>
        { return std::optional<Client::Pathfinding::PathCell>{Client::Pathfinding::PathCell{186, 115}}; };
        const auto stepped = Client::Movement::StepHeroMovement(context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 186 115"}));
        const auto& path = tested->Hero().path;
        ASSERT_GE(path.count, 2);
        const Client::Pathfinding::PathCell last = path.cells[path.count - 1U];
        if (map == 34)
        {
            EXPECT_EQ(walls[clicked], 0);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{186, 115}));
        }
        else
        {
            EXPECT_EQ(walls[clicked], 0x04);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{185, 114}));
        }
        EXPECT_EQ(runtime.chosenNpc.index, static_cast<std::int32_t>(NpcSlot));
    }
}

// 0x59ddbc → 0x4eac00 (Q-C1351): the path to a floor item (0x59dc5e, milestone 5) also carries the flag if 0x6c8e54
// still has an altar. On map 34 with stage 4 the item's wall cell goes to 0 and the path arrives; in Lorencia and
// Devias, the nearest.
TEST(NpcTalkTest, TheWalkToAFloorItemWithAChosenAltarAlsoClearsItsCell)
{
    constexpr std::int32_t FloorItem = 3;
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>();
        auto& runtime = tested->world.Runtime();
        runtime.map = map;
        runtime.crywolf.stage = 4;
        runtime.chosenNpc.index = static_cast<std::int32_t>(NpcSlot);
        tested->Npc().type = 0x205;
        runtime.selectedTargets[0] = FloorItem;
        tested->world.DrawTail().floorItems[FloorItem].position = {18650.0f, 11550.0f, 0.0f};
        auto& walls = tested->terrain.attributes->cells;
        const std::size_t item = Client::Data::Terrain::CellIndex(186, 115);
        walls[item] = 0x04;
        runtime.mouse.updateTime = 6;
        auto context = tested->Context();
        context.input.leftPressed = true;
        context.input.leftDown = true;
        const auto stepped = Client::Movement::StepHeroMovement(context);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"D3 186 115"}));
        EXPECT_EQ(runtime.floor.chosen, FloorItem);
        const auto& path = tested->Hero().path;
        ASSERT_GE(path.count, 2);
        const Client::Pathfinding::PathCell last = path.cells[path.count - 1U];
        if (map == 34)
        {
            EXPECT_EQ(walls[item], 0);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{186, 115}));
        }
        else
        {
            EXPECT_EQ(walls[item], 0x04);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{185, 114}));
        }
    }
}

// 0x5674c8..0x5695b3: stopping the hero, the own record, the C3 30 with the key and 0x6c8e54 at −1; MouseUpdateTime's
// maximum at 12 and the panel flags at 0.
TEST(NpcTalkTest, TheArrivalStopsTheHeroAndSendsTheTalk)
{
    for (const std::uint8_t map : TalkMaps)
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>(map);
        auto& runtime = tested->world.Runtime();
        runtime.panels.repairMode = 2;
        runtime.panels.field78abb09 = 1;
        runtime.byte6eeda8 = 1;
        const CrtRandom expected = Advanced(AfterTheStop(tested->random), OwnRecordRandoms + C3Randoms);
        const auto arrived = tested->Arrive();
        ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 12);
        EXPECT_EQ(runtime.chosenNpc.index, -1);
        EXPECT_EQ(runtime.panels.repairMode, 0);
        EXPECT_EQ(runtime.panels.field78abb09, 0);
        EXPECT_EQ(runtime.byte6eeda8, 0);
    }
}

// 0x5674f4 and 0x56751b: with no chosen NPC it only stops; with a +0x376 below 0xcd, 0x6c8e54 at −1 without the record.
TEST(NpcTalkTest, WithoutANpcOrWithAnotherWordTheArrivalOnlyStops)
{
    {
        const auto tested = std::make_unique<TalkingHero>();
        const CrtRandom expected = AfterTheStop(tested->random);
        tested->Hero().movement = 1;
        ASSERT_TRUE(Client::Npc::TalkToTheNpc(tested->Hero(), tested->Context()).HasValue());
        EXPECT_TRUE(tested->events.empty());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    {
        const auto tested = std::make_unique<TalkingHero>();
        tested->Hero().type = 0x154;
        tested->Hero().animation.action = 2;
        tested->Hero().movement = 1;
        const CrtRandom expected = Advanced(tested->random, 2 * Client::Models::SmallLogicalCloseRandoms + 1);
        const auto stopped = Client::Npc::TalkToTheNpc(tested->Hero(), tested->Context());
        ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
        EXPECT_TRUE(tested->events.empty());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Hero().movement, 0);
        EXPECT_EQ(tested->Hero().animation.action, 2);
    }
    {
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = 0xcc;
        tested->world.Runtime().byte6eeda8 = 1;
        const CrtRandom expected = AfterTheStop(tested->random);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_TRUE(tested->events.empty());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
        EXPECT_EQ(tested->world.Runtime().byte6eeda8, 1);
    }
}

// 0x567618: the 0xee does not talk to a hero below level 10: the warning with the sprintf, and 0x6c8e54 stays.
TEST(NpcTalkTest, TheLevelLimitedNpcNeedsLevelTen)
{
    for (const std::uint16_t level : {std::uint16_t{9}, std::uint16_t{10}})
    {
        SCOPED_TRACE(level);
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = 0xee;
        tested->world.OwnCharacter().level = level;
        ASSERT_TRUE(tested->Arrive().HasValue());
        if (level < 10)
        {
            EXPECT_TRUE(tested->events.empty());
            EXPECT_TRUE(tested->Said("hace falta el nivel 10", Client::Ui::SystemMessage));
            EXPECT_EQ(tested->world.Runtime().chosenNpc.index, 7);
        }
        else
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
            EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
        }
    }
}

// 0x567aaf..0x567ae5: the 0x101 with bit 0x2000000 of the hero's +0x78 gives the error warning, without the 30 or
// 0x6eeda8.
TEST(NpcTalkTest, TheFlagLimitedNpcRefusesTheFlaggedHero)
{
    const auto tested = std::make_unique<TalkingHero>();
    tested->Npc().word376 = 0x101;
    tested->Hero().flags78 = 0x2000000;
    tested->world.Runtime().byte6eeda8 = 1;
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_TRUE(tested->events.empty());
    EXPECT_TRUE(tested->Said("no con eso puesto", Client::Ui::ErrorMessage));
    EXPECT_EQ(tested->world.Runtime().chosenNpc.index, -1);
    EXPECT_EQ(tested->world.Runtime().byte6eeda8, 1);
}

// 0x567652..0x56766b and 0x569586: 0x78abb09 with 0xf3, 0xf6 and 0xfb; 0x6eeda8 with 0xe5.
TEST(NpcTalkTest, SomeWordsLeaveTheirFlags)
{
    for (const std::uint16_t word : std::to_array<std::uint16_t>({0xf3, 0xf6, 0xfb, 0xe5, 0xcd}))
    {
        SCOPED_TRACE(word);
        const auto tested = std::make_unique<TalkingHero>();
        tested->Npc().word376 = word;
        ASSERT_TRUE(tested->Arrive().HasValue());
        const auto& runtime = tested->world.Runtime();
        EXPECT_EQ(runtime.panels.field78abb09, word == 0xf3 || word == 0xf6 || word == 0xfb ? 1 : 0);
        EXPECT_EQ(runtime.byte6eeda8, word == 0xe5 ? 1 : 0);
        EXPECT_EQ(tested->events, (std::vector<std::string>{"30 8123"}));
    }
}

// 0x567672..0x567a85: the quest window closes with the C1 03 31 and, without the class in the quests, the A0 goes
// before the 30, each C3 with its two rand().
TEST(NpcTalkTest, TheQuestWindowClosesAndTheQuestListGoesFirst)
{
    for (const std::uint8_t map : TalkMaps)
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<TalkingHero>(map);
        auto& runtime = tested->world.Runtime();
        runtime.panels.delegated.quest = 1;
        runtime.quest.characterClass = 0xff;
        const CrtRandom expected = Advanced(AfterTheStop(tested->random), OwnRecordRandoms + C3Randoms + C3Randoms);
        const auto arrived = tested->Arrive();
        ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
        EXPECT_EQ(tested->events, (std::vector<std::string>{"31", "A0", "30 8123"}));
        EXPECT_EQ(runtime.panels.delegated.quest, 0);
        EXPECT_EQ(tested->random.State(), expected.State());
    }
}

// 0x5674d2 and 0x567b00..0x5688fd: with Crywolf active the hero does not stop; at an altar, an elf does not talk (what
// it shows, below) and the others do. On map 34 without the event, an altar does not talk, but 0x6c8e54 returns to −1;
// in Lorencia and Devias, at any stage, an altar model is just another NPC.
TEST(NpcTalkTest, CrywolfAltarsTalkOnlyWhenTheyCan)
{
    struct Case
    {
        std::int32_t map = 34;
        std::uint8_t stage = 0;
        std::uint16_t model = 0;
        std::uint8_t characterClass = 0;
        bool talks = false;
    };
    for (const Case& tested : {Case{34, 3, 0x204, 2, false}, Case{34, 4, 0x208, 0, true}, Case{34, 3, 0x1f0, 2, true},
                               Case{34, 0, 0x206, 0, false}, Case{34, 0, 0x209, 0, true}, Case{0, 3, 0x204, 2, true},
                               Case{2, 4, 0x206, 2, true}})
    {
        SCOPED_TRACE(std::format("{} {} {:#x} {}", tested.map, tested.stage, tested.model, tested.characterClass));
        const auto hero = std::make_unique<TalkingHero>();
        auto& runtime = hero->world.Runtime();
        runtime.map = tested.map;
        runtime.crywolf.stage = tested.stage;
        hero->Npc().type = tested.model;
        hero->Hero().characterClass = tested.characterClass;
        hero->texts = Client::Tests::MakeTextTable(
            {{2002, "altar tomado"}, {2003, "altar sin contratos"}, {2006, "quedan %d"}, {2007, "contratar?"}});
        const CrtRandom start = hero->random;
        const auto arrived = hero->Arrive();
        ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
        EXPECT_EQ(hero->events, tested.talks ? std::vector<std::string>{"30 8123"} : std::vector<std::string>{});
        EXPECT_EQ(runtime.chosenNpc.index, -1);
        const bool crywolf = tested.map == 34 && tested.stage != 0;
        EXPECT_EQ(hero->Hero().movement, crywolf ? 1 : 0);
        if (crywolf)
        {
            EXPECT_EQ(hero->random.State(), Advanced(start, OwnRecordRandoms + (tested.talks ? C3Randoms : 0)).State());
        }
    }
}

// 0x567b4c..0x567c23 (CL9 milestone 11): an elf at an altar, with Crywolf active, does not send the C1 30: with the
// altar contracted, 0x435b00(0x34); with no contracts, 0x435b00(0x35); otherwise, 0x435b00(0x38) with the contracts
// left and question 0x39 with the altar's key. All three go on at 0x569573 (0x6eeda8 by the +0x376 and 0x6c8e54 at −1).
TEST(NpcTalkTest, AnElfAtACrywolfAltarSeesItsStateInsteadOfTalking)
{
    struct Case
    {
        std::uint8_t altar = 0;
        std::string_view first;
        std::string_view second;
        std::uint8_t window = 0;
        std::int32_t value = 0;
    };
    constexpr std::uint16_t AltarKey = 0x8140;
    for (const Case& tested : {Case{0x13, "altar tomado", "", 2, 0}, Case{0x20, "altar sin contratos", "", 2, 0},
                               Case{0x03, "quedan 3", "contratar?", 1, static_cast<std::int16_t>(AltarKey)}})
    {
        SCOPED_TRACE(std::format("{:#x}", tested.altar));
        for (const std::uint8_t stage : {std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(stage);
            const auto hero = std::make_unique<TalkingHero>();
            auto& runtime = hero->world.Runtime();
            runtime.map = 34;
            runtime.crywolf.stage = stage;
            runtime.crywolf.ui.altars[2] = tested.altar;
            hero->Npc().type = 0x206;
            hero->Npc().key = AltarKey;
            hero->Npc().word376 = 0xe5;
            hero->Hero().characterClass = 2;
            hero->texts = Client::Tests::MakeTextTable(
                {{2002, "altar tomado"}, {2003, "altar sin contratos"}, {2006, "quedan %d"}, {2007, "contratar?"}});
            const CrtRandom start = hero->random;
            const auto arrived = hero->Arrive();
            ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
            EXPECT_TRUE(hero->events.empty());
            const auto& ui = runtime.crywolf.ui;
            EXPECT_EQ(std::string_view(ui.contractLines[0].data()), tested.first);
            EXPECT_EQ(std::string_view(ui.contractLines[1].data()), tested.second);
            EXPECT_EQ(ui.byte723a44, tested.window);
            EXPECT_EQ(ui.dwords723bdc[1], tested.value);
            EXPECT_EQ(runtime.chosenNpc.index, -1);
            EXPECT_EQ(runtime.byte6eeda8, 1);
            EXPECT_EQ(hero->Hero().movement, 1);
            EXPECT_EQ(hero->random.State(), Advanced(start, OwnRecordRandoms).State());
        }
    }
}

// The path to a Crywolf altar with the repo's Data (0x59dc32 → 0x4eac00 → 0x4ead70), in Lorencia, Devias and Crywolf,
// with stage 4 in all three. The altar is on a cell the .att has as a wall (0x04, alone among free cells), with the
// character's 0x02 (0x509165), and the elf four cells east. Only on map 34 does the flag (0x4eac41) zero the altar's
// cell: the exact A* reaches it and the arrival (0x567b00) shows the contract offer. On maps 0 and 2 the exact one
// fails at the goal (0x4eae38), the nearest stops at the upper diagonal and the arrival talks to it with the C1 30.
class NpcTalkDataTest : public Client::Tests::GoldenDataTest
{
};

TEST_F(NpcTalkDataTest, OnlyInCrywolfTheWalkReachesTheAltarCell)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t altarX;
        std::uint8_t altarY;
    };
    constexpr std::int32_t CrywolfMap = 34;
    constexpr std::uint8_t Away = 4;
    for (const Case& probe : {Case{0, 172, 47}, Case{2, 181, 109}, Case{CrywolfMap, 116, 114}})
    {
        SCOPED_TRACE(probe.map);
        const auto tested = std::make_unique<TalkingHero>();
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, static_cast<std::uint8_t>(probe.map));
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        tested->terrain = std::move(loaded).Value();
        auto& runtime = tested->world.Runtime();
        runtime.map = probe.map;
        runtime.crywolf.stage = 4;
        const auto heroX = static_cast<std::uint8_t>(probe.altarX + Away);
        const auto center = [](std::uint8_t cell) { return (static_cast<float>(cell) * 100.0f) + 50.0f; };
        ActorState& hero = tested->Hero();
        hero.cellX = hero.targetX = heroX;
        hero.cellY = hero.targetY = probe.altarY;
        hero.position = {center(heroX), center(probe.altarY), 0.0f};
        hero.characterClass = 2;
        ActorState& altar = tested->Npc();
        altar.type = 0x206;
        altar.key = 0x013e;
        altar.position = {center(probe.altarX), center(probe.altarY), 0.0f};
        auto& walls = tested->terrain.attributes->cells;
        const std::size_t cell = Client::Data::Terrain::CellIndex(probe.altarX, probe.altarY);
        ASSERT_EQ(walls[cell], Client::Pathfinding::BlockedWallBit);
        for (std::uint8_t x = probe.altarX + 1; x <= heroX; ++x)
            ASSERT_LT(walls[Client::Data::Terrain::CellIndex(x, probe.altarY)], 2) << int{x};
        Client::Pathfinding::MarkCharacterWall(walls, probe.altarX, probe.altarY);
        tested->texts = Client::Tests::MakeTextTable({{2006, "quedan %d"}, {2007, "contratar?"}});

        const auto clicked = tested->Click();
        ASSERT_TRUE(clicked.HasValue()) << clicked.Error();
        const Client::Pathfinding::Path path = hero.path;
        ASSERT_GE(path.count, 2);
        const Client::Pathfinding::PathCell last = path.cells[path.count - 1U];
        if (probe.map == CrywolfMap)
        {
            EXPECT_EQ(walls[cell], 0);
            EXPECT_EQ(path.count, Away + 1);
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{probe.altarX, probe.altarY}));
        }
        else
        {
            EXPECT_EQ(walls[cell], Client::Pathfinding::BlockedWallBit | Client::Pathfinding::CharacterWallBit);
            // The nearest: the upper diagonal, as in TheWalkToACrywolfAltarClearsItsCell.
            EXPECT_EQ(last, (Client::Pathfinding::PathCell{static_cast<std::uint8_t>(probe.altarX + 1),
                                                           static_cast<std::uint8_t>(probe.altarY - 1)}));
        }
        constexpr int StepLimit = 100;
        for (int step = 0; step < StepLimit && hero.movement != 0; ++step)
        {
            const auto stepped = Client::Movement::StepHeroMovement(tested->Context());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_EQ(hero.movement, 0);
        EXPECT_EQ(hero.cellX, last.x);
        EXPECT_EQ(hero.cellY, last.y);
        EXPECT_EQ(runtime.chosenNpc.index, -1);
        const std::string walk = std::format("D3 {} {}", probe.altarX, probe.altarY);
        const auto& ui = runtime.crywolf.ui;
        if (probe.map == CrywolfMap)
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{walk}));
            EXPECT_EQ(std::string_view(ui.contractLines[0].data()), "quedan 2");
            EXPECT_EQ(std::string_view(ui.contractLines[1].data()), "contratar?");
            EXPECT_EQ(ui.byte723a44, 1);
            EXPECT_EQ(ui.dwords723bdc[1], 0x013e);
        }
        else
        {
            EXPECT_EQ(tested->events, (std::vector<std::string>{walk, "30 013e"}));
            EXPECT_EQ(ui.byte723a44, 0);
        }
    }
}
} // namespace
