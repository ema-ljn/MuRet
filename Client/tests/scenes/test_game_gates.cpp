#include "Scenes/GameGates.h"

#include "Core/Expected.h"
#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateDecision.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "support/GateFiles.h"
#include "support/GoldenDir.h"
#include "support/FileBytes.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Data::Tables::GateRecord;
using Client::Data::Tables::GateTable;
using Client::GameLogic::GateWarning;
using Client::Scenes::CheckGameGates;
using Client::Scenes::GameGateContext;
using Client::Tests::AfterDraws;
using Client::Tests::GateTableBytes;

constexpr GateRecord Entry{1, 0, 10, 20, 12, 22, 2, 7, 10};

struct Harness
{
    explicit Harness(std::initializer_list<std::pair<std::size_t, GateRecord>> records = {{23, Entry}})
        : gates(GateTableBytes(records))
    {
        world->Runtime().map = 0;
        world->Actors()[0].cellX = 11;
        world->Actors()[0].cellY = 21;
        world->OwnCharacter().level = 10;
        world->OwnCharacter().equipment[7].id = 0xffff;
        world->OwnCharacter().equipment[8].id = 0xffff;
        context.requestGate = [this](std::uint8_t gate) { sent.push_back(gate); };
    }
    std::unique_ptr<Client::Scenes::WorldState> world = std::make_unique<Client::Scenes::WorldState>();
    Client::Render::Models::CrtRandom random{1};
    GateTable gates;
    std::vector<std::uint8_t> sent;
    GameGateContext context{.world = *world, .gates = gates, .random = random, .ticksMs = 3000};
};

// 0x599f41..0x59a038: X>=min, Y>=min, X<=max, Y<=max, luego contador.
TEST(GameGatesTest, RectangleTestsConsumeOnlyTheShortCircuitPrefix)
{
    struct Case
    {
        std::uint8_t x;
        std::uint8_t y;
        int draws;
    };
    for (const auto test : {Case{9, 21, 4}, Case{11, 19, 8}, Case{13, 21, 12}, Case{11, 23, 16}, Case{11, 21, 20}})
    {
        Harness tested;
        tested.world->Actors()[0].cellX = test.x;
        tested.world->Actors()[0].cellY = test.y;
        tested.world->Runtime().warpCounter = 1;
        ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_EQ(tested.random.State(), AfterDraws(1, test.draws).State());
    }
}

TEST(GameGatesTest, WrongFlagAndMapNeverReadTheCells)
{
    GateRecord arrival = Entry;
    arrival.flag = 2;
    GateRecord elsewhere = Entry;
    elsewhere.map = 1;
    Harness tested({{1, arrival}, {2, elsewhere}});
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 0).State());
    EXPECT_TRUE(tested.sent.empty());
}

TEST(GameGatesTest, ABlockedHeroStillReadsTheRectangleAndCounter)
{
    Harness tested;
    tested.world->Actors()[0].byte399 = 1;
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_TRUE(tested.sent.empty());
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 20).State());
}

// Sigue el barrido aun después de poner la espera: 0x59adcc vuelve hasta el gate 255.
TEST(GameGatesTest, ARequestedGatePreservesSendCounterAndOwnRecordOrderAndKeepsScanning)
{
    Harness tested({{23, Entry}, {24, Entry}});
    tested.world->Runtime().selectedTargets.fill(9);
    tested.world->Runtime().movement.movementTarget = 8;
    tested.context.requestGate = [&](std::uint8_t gate)
    {
        tested.sent.push_back(gate);
        EXPECT_EQ(tested.world->Runtime().warpCounter, 0);
        EXPECT_EQ(tested.world->Runtime().ownRecordDepth, 1);
        EXPECT_FALSE(tested.world->Runtime().movement.warpPending);
        EXPECT_EQ(tested.world->Runtime().selectedTargets[0], 9);
        EXPECT_EQ(tested.random.State(), AfterDraws(1, 24).State());
        static_cast<void>(tested.random.Next());
    };
    const auto result = CheckGameGates(tested.context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{23}));
    EXPECT_EQ(result.Value().request, 23);
    EXPECT_EQ(tested.world->Runtime().warpCounter, 9'999'999);
    EXPECT_TRUE(tested.world->Runtime().movement.warpPending);
    EXPECT_EQ(tested.world->Runtime().movement.movementTarget, 0);
    EXPECT_EQ(tested.world->Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    EXPECT_EQ(tested.world->Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 3897).State());
}

TEST(GameGatesTest, ALevelFailureSetsFiftyAndKeepsScanningWithNoSecondOwnAccess)
{
    Harness tested({{23, Entry}, {24, Entry}});
    tested.world->OwnCharacter().level = 9;
    const auto result = CheckGameGates(tested.context);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.sent.empty());
    EXPECT_EQ(result.Value().warnings, (std::vector<GateWarning>{{350, std::uint16_t{10}}}));
    EXPECT_EQ(result.Value().waitSteps, 50);
    EXPECT_EQ(tested.world->Runtime().warpCounter, 50);
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 3892).State());
}

TEST(GameGatesTest, GateTwentyEightHasItsOwnSilentLevelAccessBeforeTheOrdinaryAccess)
{
    Harness low({{28, Entry}});
    low.world->OwnCharacter().level = 9;
    const auto failed = CheckGameGates(low.context);
    ASSERT_TRUE(failed.HasValue());
    EXPECT_TRUE(failed.Value().warnings.empty());
    EXPECT_TRUE(low.sent.empty());
    EXPECT_EQ(low.world->Runtime().warpCounter, 0);
    EXPECT_EQ(low.random.State(), AfterDraws(1, 3868).State());
    Harness high({{28, Entry}});
    const auto accepted = CheckGameGates(high.context);
    ASSERT_TRUE(accepted.HasValue());
    EXPECT_EQ(high.sent, (std::vector<std::uint8_t>{28}));
    EXPECT_EQ(high.random.State(), AfterDraws(1, 7724).State());
}

TEST(GameGatesTest, AnOverlappingGateCanSendAfterTheFirstClearsPending)
{
    Harness tested({{23, Entry}, {24, Entry}});
    tested.world->Runtime().movement.warpPending = true;
    const auto result = CheckGameGates(tested.context);
    ASSERT_TRUE(result.HasValue());
    EXPECT_TRUE(result.Value().clearWarpPending);
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{24}));
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 7748).State());
}

TEST(GameGatesTest, GateZeroSetsItsRequestBlockBeforeSending)
{
    Harness tested({{0, Entry}});
    tested.context.requestGate = [&](std::uint8_t gate)
    {
        tested.sent.push_back(gate);
        EXPECT_EQ(tested.world->Runtime().movement.gateRequestBlocked, 1);
        EXPECT_EQ(tested.world->Runtime().warpCounter, 0);
    };
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{0}));
}

TEST(GameGatesTest, TheThreeSecondWindowUsesUnsignedElapsedTicks)
{
    Harness early;
    early.context.ticksMs = 2999;
    const auto denied = CheckGameGates(early.context);
    ASSERT_TRUE(denied.HasValue());
    EXPECT_TRUE(denied.Value().clearWarpPending);
    EXPECT_TRUE(early.sent.empty());
    EXPECT_EQ(early.random.State(), AfterDraws(1, 3872).State());
    Harness wrapped;
    wrapped.world->Runtime().movement.lastTeleportMs = 0xfffff000u;
    wrapped.context.ticksMs = 0x800;
    ASSERT_TRUE(CheckGameGates(wrapped.context).HasValue());
    EXPECT_EQ(wrapped.sent, (std::vector<std::uint8_t>{23}));
}

TEST(GameGatesTest, EquipmentWarningsUseOwnItemTypesAndDoNotSetTheWait)
{
    Harness atlans({{45, Entry}});
    atlans.world->OwnCharacter().equipment[8].id = 0x1a02;
    const auto blocked = CheckGameGates(atlans.context);
    ASSERT_TRUE(blocked.HasValue());
    EXPECT_EQ(blocked.Value().warnings, (std::vector<GateWarning>{{261, std::nullopt}}));
    EXPECT_TRUE(atlans.sent.empty());
    EXPECT_EQ(atlans.world->Runtime().warpCounter, 0);
    EXPECT_EQ(atlans.random.State(), AfterDraws(1, 3868).State());
    Harness icarus({{62, Entry}});
    icarus.world->OwnCharacter().level = 9;
    const auto grounded = CheckGameGates(icarus.context);
    ASSERT_TRUE(grounded.HasValue());
    EXPECT_EQ(grounded.Value().warnings, (std::vector<GateWarning>{{263, std::nullopt}, {350, std::uint16_t{10}}}));
    EXPECT_EQ(icarus.world->Runtime().warpCounter, 0);
}

TEST(GameGatesTest, TheLevelDiscountUsesThePublishedActorsClass)
{
    Harness tested;
    tested.world->Actors()[0].characterClass = 3;
    tested.world->OwnCharacter().characterClass = 0;
    tested.world->OwnCharacter().level = 6;
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{23}));
}

TEST(GameGatesTest, ExistingAccessesSuppressAllLogicalClosures)
{
    Harness tested;
    tested.world->Actors()[0].cellXAccesses = 1;
    tested.world->Actors()[0].cellYAccesses = 1;
    tested.world->Runtime().warpDepth = 1;
    tested.world->Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{23}));
    EXPECT_EQ(tested.random.State(), AfterDraws(1, 0).State());
    EXPECT_EQ(tested.world->Runtime().ownRecordDepth, 1);
}

TEST(GameGatesTest, SiegeMacroBoundaryOccursAfterSendAndWaitBeforePendingAndTargetReset)
{
    GateRecord destination{};
    destination.map = 30;
    Harness tested({{23, Entry}, {2, destination}});
    tested.world->Runtime().selectedTargets.fill(9);
    tested.world->Runtime().movement.movementTarget = 8;
    const auto result = CheckGameGates(tested.context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x61c210"), std::string::npos);
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{23}));
    EXPECT_EQ(tested.world->Runtime().warpCounter, 9'999'999);
    EXPECT_FALSE(tested.world->Runtime().movement.warpPending);
    EXPECT_EQ(tested.world->Runtime().selectedTargets[0], 9);
    EXPECT_EQ(tested.world->Runtime().movement.movementTarget, 8);
}
TEST(GameGatesTest, AtlansChecksTheAlternateMountOnlyWithAPositiveCursorCount)
{
    Harness inactive({{45, Entry}});
    inactive.context.alternateMount = 0x1a02;
    inactive.world->Runtime().cursorItemCount = -1;
    ASSERT_TRUE(CheckGameGates(inactive.context).HasValue());
    EXPECT_EQ(inactive.sent, (std::vector<std::uint8_t>{45}));
    Harness active({{45, Entry}});
    active.context.alternateMount = 0x1a02;
    active.world->Runtime().cursorItemCount = 1;
    const auto blocked = CheckGameGates(active.context);
    ASSERT_TRUE(blocked.HasValue());
    EXPECT_TRUE(active.sent.empty());
    EXPECT_EQ(blocked.Value().warnings, (std::vector<GateWarning>{{261, std::nullopt}}));
}

TEST(GameGatesTest, IcarusReadsTheOwnWingsTypeRatherThanTheActorsModel)
{
    Harness tested({{62, Entry}});
    tested.world->OwnCharacter().equipment[7].id = 0x1800;
    tested.world->Actors()[0].wings.model = 0xffff;
    ASSERT_TRUE(CheckGameGates(tested.context).HasValue());
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{62}));
}

TEST(GameGatesTest, ARequestWithoutTheSendServiceStopsBeforeTheWait)
{
    Harness tested;
    tested.context.requestGate = {};
    const auto result = CheckGameGates(tested.context);
    EXPECT_FALSE(result.HasValue());
    EXPECT_TRUE(tested.sent.empty());
    EXPECT_EQ(tested.world->Runtime().warpCounter, 0);
    EXPECT_FALSE(tested.world->Runtime().movement.warpPending);
}

using GoldenGameGates = Client::Tests::GoldenDataTest;

TEST_F(GoldenGameGates, GateTwentyThreeUsesTheRealLorenciaRectangleAndLevel)
{
    const auto path = DataFile("Gate.bmd");
    ASSERT_TRUE(path.has_value());
    const auto file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = Client::Data::Tables::LoadGateTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    for (const std::uint16_t level : {std::uint16_t{9}, std::uint16_t{10}})
    {
        Harness tested;
        tested.gates = loaded.Value();
        tested.world->Actors()[0].cellX = 215;
        tested.world->Actors()[0].cellY = 246;
        tested.world->OwnCharacter().level = level;
        const auto result = CheckGameGates(tested.context);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        if (level == 9)
        {
            EXPECT_TRUE(tested.sent.empty());
            EXPECT_EQ(result.Value().warnings, (std::vector<GateWarning>{{350, std::uint16_t{10}}}));
            EXPECT_EQ(tested.world->Runtime().warpCounter, 50);
        }
        else
        {
            EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{23}));
            EXPECT_EQ(tested.world->Runtime().warpCounter, 9'999'999);
            EXPECT_TRUE(tested.world->Runtime().movement.warpPending);
        }
    }
}

TEST_F(GoldenGameGates, TheRealGate102ReportsTheMacroBoundaryAfterSendingItsOwnIndex)
{
    const auto path = DataFile("Gate.bmd");
    ASSERT_TRUE(path.has_value());
    const auto file = Client::Tests::ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = Client::Data::Tables::LoadGateTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    Harness tested;
    tested.gates = loaded.Value();
    tested.world->Actors()[0].cellX = 239;
    tested.world->Actors()[0].cellY = 14;
    const auto result = CheckGameGates(tested.context);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x61c210"), std::string::npos);
    EXPECT_EQ(tested.sent, (std::vector<std::uint8_t>{102}));
    EXPECT_EQ(tested.world->Runtime().warpCounter, 9'999'999);
    EXPECT_FALSE(tested.world->Runtime().movement.warpPending);
}

} // namespace
