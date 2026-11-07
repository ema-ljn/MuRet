#include "GameLogic/GateCheck.h"

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateCheckState.h"
#include "GameLogic/GateDecision.h"
#include "support/GateFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Casos del ASM de 0x599ef0: la decisión no ejecuta el original ni simula sus accesos protegidos.
namespace
{
using Client::Data::Tables::GateRecord;
using Client::Data::Tables::GateTable;
using Client::GameLogic::GateCheck;
using Client::GameLogic::GateCheckState;
using Client::GameLogic::GateDecision;
using Client::GameLogic::GateWarning;
using Client::Tests::GateTableBytes;

constexpr GateRecord Entry{1, 0, 10, 20, 12, 22, 2, 7, 20};

GateCheckState Ready()
{
    return {.x = 11, .y = 21, .level = 20, .elapsedMs = 3000};
}

GateDecision Check(std::size_t index, const GateCheckState& state, GateRecord record = Entry)
{
    return GateCheck(GateTable(GateTableBytes({{index, record}})), state);
}

void ExpectNothing(const GateDecision& decision)
{
    EXPECT_EQ(decision.request, std::nullopt);
    EXPECT_EQ(decision.warnings.size(), 0);
    EXPECT_EQ(decision.waitSteps, 0);
}

void ExpectWarning(const GateDecision& decision, std::uint16_t textId, std::uint8_t waitSteps = 0,
                   std::optional<std::uint16_t> requiredLevel = std::nullopt)
{
    EXPECT_EQ(decision.request, std::nullopt);
    ASSERT_EQ(decision.warnings.size(), 1);
    EXPECT_EQ(decision.warnings[0], (GateWarning{textId, requiredLevel}));
    EXPECT_EQ(decision.waitSteps, waitSteps);
}

TEST(GateCheckTest, RequestsTheEntryIndexRatherThanItsTarget)
{
    const auto decision = Check(1, Ready());

    EXPECT_EQ(decision.request, 1);
    EXPECT_EQ(decision.warnings.size(), 0);
    EXPECT_EQ(decision.waitSteps, 0);
}

TEST(GateCheckTest, RequestsTheFirstAndLastIndicesOnANonzeroMap)
{
    auto state = Ready();
    state.map = 7;
    auto record = Entry;
    record.map = 7;
    for (const std::size_t index : {0u, 255u})
    {
        const auto decision = Check(index, state, record);
        EXPECT_EQ(decision.request, index);
        EXPECT_TRUE(decision.warnings.empty());
        EXPECT_EQ(decision.waitSteps, 0);
    }
}

TEST(GateCheckTest, TheCurrentMapIsComparedWithoutByteTruncation)
{
    auto state = Ready();
    state.map = 256;

    const auto decision = Check(1, state);
    ExpectNothing(decision);
    EXPECT_FALSE(decision.clearWarpPending);
}

TEST(GateCheckTest, RectangleIsInclusiveAndDoesNotHaveTheServerMargin)
{
    for (const std::int32_t x : {10, 12})
    {
        for (const std::int32_t y : {20, 22})
        {
            auto state = Ready();
            state.x = x;
            state.y = y;
            EXPECT_EQ(Check(1, state).request, 1);
        }
    }
    for (const std::int32_t x : {-1, 9, 13, 256})
    {
        auto state = Ready();
        state.x = x;
        const auto decision = Check(1, state);
        ExpectNothing(decision);
        EXPECT_FALSE(decision.clearWarpPending);
    }
    for (const std::int32_t y : {19, 23})
    {
        auto state = Ready();
        state.y = y;
        const auto decision = Check(1, state);
        ExpectNothing(decision);
        EXPECT_FALSE(decision.clearWarpPending);
    }
}

TEST(GateCheckTest, OnlyFlagOneAndTheCurrentMapQualify)
{
    for (const int flag : {0, 2, 255})
    {
        auto record = Entry;
        record.flag = static_cast<std::uint8_t>(flag);
        const auto decision = Check(1, Ready(), record);
        ExpectNothing(decision);
        EXPECT_FALSE(decision.clearWarpPending);
    }
    auto state = Ready();
    state.map = 1;
    const auto otherMap = Check(1, state);
    ExpectNothing(otherMap);
    EXPECT_FALSE(otherMap.clearWarpPending);
    state.map = -1;
    const auto negativeMap = Check(1, state);
    ExpectNothing(negativeMap);
    EXPECT_FALSE(negativeMap.clearWarpPending);
}

TEST(GateCheckTest, ReversedRectangleRemainsEmpty)
{
    auto record = Entry;
    record.y1 = 22;
    record.y2 = 20;
    const auto decision = Check(1, Ready(), record);
    ExpectNothing(decision);
    EXPECT_FALSE(decision.clearWarpPending);
}

TEST(GateCheckTest, LowLevelWarnsAndWaitsFiftySteps)
{
    auto state = Ready();
    state.level = 19;

    const auto decision = Check(1, state);
    ExpectWarning(decision, 350, 50, std::uint16_t{20});
    EXPECT_FALSE(decision.clearWarpPending);
}

TEST(GateCheckTest, OnlyBaseClassesThreeAndFourUseTruncatedTwoThirds)
{
    auto state = Ready();
    for (const int characterClass : {3, 4, 11, 12})
    {
        state.characterClass = static_cast<std::uint8_t>(characterClass);
        state.level = 13;
        EXPECT_EQ(Check(1, state).request, 1);
        state.level = 12;
        ExpectWarning(Check(1, state), 350, 50, std::uint16_t{13});
    }
    for (const int characterClass : {0, 1, 2, 5, 6, 7})
    {
        state.characterClass = static_cast<std::uint8_t>(characterClass);
        state.level = 19;
        ExpectWarning(Check(1, state), 350, 50, std::uint16_t{20});
    }
}

TEST(GateCheckTest, GateTwentyEightIsSilentBelowItsRequiredLevel)
{
    auto state = Ready();
    state.level = 19;
    const auto decision = Check(28, state);
    ExpectNothing(decision);
    EXPECT_FALSE(decision.clearWarpPending);
    state.level = 20;
    EXPECT_EQ(Check(28, state).request, 28);
}

TEST(GateCheckTest, GateTwentyEightKeepsItsReducedLevelRuleWhileRidingUniria)
{
    for (const int characterClass : {3, 4})
    {
        auto state = Ready();
        state.characterClass = static_cast<std::uint8_t>(characterClass);
        state.mount = 0x1a02;
        state.level = 12;
        const auto silent = Check(28, state);
        ExpectNothing(silent);
        EXPECT_FALSE(silent.clearWarpPending);
        state.level = 13;
        const auto decision = Check(28, state);
        EXPECT_EQ(decision.request, 28);
        EXPECT_TRUE(decision.warnings.empty());
    }
}

TEST(GateCheckTest, AtlansRejectsUniriaAndDinorantBeforeCheckingLevel)
{
    for (const std::size_t index : {45u, 46u, 47u, 48u, 49u, 55u, 56u})
    {
        for (const int mount : {0x1a02, 0x1a03})
        {
            auto state = Ready();
            state.mount = static_cast<std::int16_t>(mount);
            state.level = 0;
            const auto decision = Check(index, state);
            ExpectWarning(decision, 261);
            EXPECT_FALSE(decision.clearWarpPending);
        }
    }
    for (const std::size_t index : {44u, 50u, 54u, 57u})
    {
        auto state = Ready();
        state.mount = 0x1a02;
        EXPECT_EQ(Check(index, state).request, index);
    }
}

TEST(GateCheckTest, AtlansAlsoChecksTheActiveAlternateMount)
{
    for (const int mount : {0x1a02, 0x1a03})
    {
        auto state = Ready();
        state.alternateMount = static_cast<std::int16_t>(mount);
        EXPECT_EQ(Check(45, state).request, 45);
        state.alternateMountActive = true;
        const auto decision = Check(45, state);
        ExpectWarning(decision, 261);
        EXPECT_FALSE(decision.clearWarpPending);
    }
    for (const int mount : {-1, 0x1a01, 0x1a04, 32767})
    {
        auto state = Ready();
        state.mount = static_cast<std::int16_t>(mount);
        state.alternateMount = static_cast<std::int16_t>(mount);
        state.alternateMountActive = true;
        EXPECT_EQ(Check(45, state).request, 45);
    }
}

TEST(GateCheckTest, AtlansRejectsTheCurrentMountEvenWithAnInvalidActiveAlternate)
{
    auto state = Ready();
    state.mount = 0x1a02;
    state.alternateMount = -1;
    state.alternateMountActive = true;

    ExpectWarning(Check(45, state), 261);
}

TEST(GateCheckTest, TheAlternateMountAffectsOnlyAtlans)
{
    struct PreviewCase
    {
        std::size_t index;
        std::int16_t wings;
        std::int16_t mount;
        std::int16_t alternateMount;
        std::uint16_t warning;
    };
    constexpr std::array<PreviewCase, 7> Cases{{
        {1, -1, -1, 0x1a02, 0},
        {1, -1, -1, 0x1a03, 0},
        {28, -1, -1, 0x1a02, 0},
        {28, -1, -1, 0x1a03, 0},
        {62, -1, -1, 0x1a03, 263},
        {62, 0x1800, -1, 0x1a02, 0},
        {65, 0x1a1e, 0x1a02, -1, 569},
    }};
    for (const PreviewCase& testCase : Cases)
    {
        SCOPED_TRACE(testCase.index);
        SCOPED_TRACE(testCase.alternateMount);
        auto state = Ready();
        state.wings = testCase.wings;
        state.mount = testCase.mount;
        state.alternateMount = testCase.alternateMount;
        state.alternateMountActive = true;
        const auto decision = Check(testCase.index, state);
        if (testCase.warning != 0)
        {
            ExpectWarning(decision, testCase.warning);
            continue;
        }
        EXPECT_EQ(decision.request, testCase.index);
        EXPECT_TRUE(decision.warnings.empty());
        EXPECT_EQ(decision.waitSteps, 0);
    }
}

TEST(GateCheckTest, IcarusWithoutFlightWarnsAndDoesNotSetTheWarpCounter)
{
    for (const std::size_t index : {62u, 63u, 64u, 65u})
    {
        ExpectWarning(Check(index, Ready()), 263);
    }
    auto state = Ready();
    state.level = 19;
    const auto decision = Check(62, state);
    EXPECT_EQ(decision.request, std::nullopt);
    ASSERT_EQ(decision.warnings.size(), 2);
    EXPECT_EQ(decision.warnings[0], (GateWarning{263, std::nullopt}));
    EXPECT_EQ(decision.warnings[1], (GateWarning{350, std::uint16_t{20}}));
    EXPECT_EQ(decision.waitSteps, 0);
}

TEST(GateCheckTest, IcarusWithoutFlightReportsTheReducedLevelForClassesThreeAndFour)
{
    for (const int characterClass : {3, 4})
    {
        auto state = Ready();
        state.characterClass = static_cast<std::uint8_t>(characterClass);
        state.level = 12;
        const auto decision = Check(62, state);
        EXPECT_EQ(decision.request, std::nullopt);
        EXPECT_EQ(decision.warnings, (std::vector<GateWarning>{{263, std::nullopt}, {350, std::uint16_t{13}}}));
        EXPECT_EQ(decision.waitSteps, 0);
        state.level = 13;
        ExpectWarning(Check(62, state), 263);
    }
}

TEST(GateCheckTest, IcarusAcceptsExactlyTheWingsCapeAndFlyingMountTypes)
{
    for (const int wings : {0x1800, 0x1801, 0x1802, 0x1803, 0x1804, 0x1805, 0x1806, 0x1a1e})
    {
        auto state = Ready();
        state.wings = static_cast<std::int16_t>(wings);
        EXPECT_EQ(Check(62, state).request, 62);
    }
    for (const int wings : {-1, 0x17ff, 0x1807, 0x1a1d, 0x1a1f})
    {
        auto state = Ready();
        state.wings = static_cast<std::int16_t>(wings);
        ExpectWarning(Check(62, state), 263);
    }
    for (const int mount : {0x1a03, 0x1a04, 0x1a25})
    {
        auto state = Ready();
        state.mount = static_cast<std::int16_t>(mount);
        EXPECT_EQ(Check(62, state).request, 62);
    }
    for (const std::size_t index : {61u, 66u})
    {
        EXPECT_EQ(Check(index, Ready()).request, index);
    }
}

TEST(GateCheckTest, IcarusUniriaWarningRequiresFlightAndPrecedesLevel)
{
    auto state = Ready();
    state.mount = 0x1a02;
    ExpectWarning(Check(62, state), 263);
    state.wings = 0x1800;
    state.level = 0;
    ExpectWarning(Check(62, state), 569);
    state.mount = -1;
    ExpectWarning(Check(62, state), 350, 50, std::uint16_t{20});
}

TEST(GateCheckTest, WaitsForThreeSecondsAfterTheLastTeleport)
{
    auto state = Ready();
    for (const std::uint32_t elapsed : {0u, 2999u})
    {
        state.elapsedMs = elapsed;
        const auto decision = Check(1, state);
        ExpectNothing(decision);
        EXPECT_TRUE(decision.clearWarpPending);
    }
    for (const std::uint32_t elapsed : {3000u, 3001u, 0xffffffffu})
    {
        state.elapsedMs = elapsed;
        EXPECT_EQ(Check(1, state).request, 1);
    }
}

TEST(GateCheckTest, CounterMustBeZeroIncludingTheThirtyStepMovementBoundary)
{
    auto state = Ready();
    for (const std::int32_t counter : {-1, 1, 29, 30, 31, 50, 9999999})
    {
        state.warpCounter = counter;
        const auto decision = Check(1, state);
        ExpectNothing(decision);
        EXPECT_FALSE(decision.clearWarpPending);
    }
    state.warpCounter = 0;
    EXPECT_EQ(Check(1, state).request, 1);
}

TEST(GateCheckTest, ExplicitHeroAndRequestGuardsKeepTheirDifferentPrecedence)
{
    auto state = Ready();
    state.heroGateBlocked = true;
    state.level = 0;
    const auto heroBlocked = Check(1, state);
    ExpectNothing(heroBlocked);
    EXPECT_FALSE(heroBlocked.clearWarpPending);
    state.heroGateBlocked = false;
    state.requestBlocked = true;
    const auto lowLevel = Check(1, state);
    ExpectWarning(lowLevel, 350, 50, std::uint16_t{20});
    EXPECT_FALSE(lowLevel.clearWarpPending);
    state.level = 20;
    const auto requestBlocked = Check(1, state);
    ExpectNothing(requestBlocked);
    EXPECT_TRUE(requestBlocked.clearWarpPending);
    state.requestBlocked = false;
    state.warpPending = true;
    const auto pending = Check(1, state);
    ExpectNothing(pending);
    EXPECT_TRUE(pending.clearWarpPending);
}

TEST(GateCheckTest, OverlapRequestsOnlyTheFirstEligibleGate)
{
    const GateTable table(GateTableBytes({{2, Entry}, {1, Entry}}));
    EXPECT_EQ(GateCheck(table, Ready()).request, 1);

    auto state = Ready();
    state.level = 19;
    const GateTable silentThenEntry(GateTableBytes({{28, Entry}, {29, Entry}}));
    ExpectWarning(GateCheck(silentThenEntry, state), 350, 50, std::uint16_t{20});
}

TEST(GateCheckTest, OverlappingEquipmentWarningsPrecedeALaterRequest)
{
    const GateTable table(GateTableBytes({{45, Entry}, {46, Entry}, {62, Entry}, {70, Entry}}));
    auto state = Ready();
    state.mount = 0x1a02;

    const auto decision = GateCheck(table, state);

    EXPECT_EQ(decision.request, 70);
    EXPECT_EQ(decision.warnings,
              (std::vector<GateWarning>{{261, std::nullopt}, {261, std::nullopt}, {263, std::nullopt}}));
    EXPECT_EQ(decision.waitSteps, 0);
}

TEST(GateCheckTest, EveryIcarusWarningIsKeptBeforeALaterLevelWait)
{
    const GateTable table(GateTableBytes({{62, Entry}, {63, Entry}, {70, Entry}, {71, Entry}}));
    auto state = Ready();
    state.level = 0;

    const auto decision = GateCheck(table, state);

    EXPECT_EQ(decision.request, std::nullopt);
    EXPECT_EQ(decision.warnings, (std::vector<GateWarning>{{263, std::nullopt},
                                                           {350, std::uint16_t{20}},
                                                           {263, std::nullopt},
                                                           {350, std::uint16_t{20}},
                                                           {350, std::uint16_t{20}}}));
    EXPECT_EQ(decision.waitSteps, 50);
}

TEST(GateCheckTest, ARejectedRequestClearsPendingBeforeTheNextOverlappingGate)
{
    const GateTable table(GateTableBytes({{1, Entry}, {2, Entry}, {3, Entry}}));
    auto state = Ready();
    state.warpPending = true;

    const auto decision = GateCheck(table, state);

    EXPECT_TRUE(decision.clearWarpPending);
    EXPECT_EQ(decision.request, 2);
}

TEST(GateCheckTest, ARejectedRequestReportsThePendingStoreEvenIfItWasAlreadyZero)
{
    auto state = Ready();
    state.elapsedMs = 2999;

    const auto decision = Check(1, state);

    ExpectNothing(decision);
    EXPECT_TRUE(decision.clearWarpPending);
    EXPECT_FALSE(Check(1, Ready()).clearWarpPending);
    EXPECT_FALSE(Check(62, state).clearWarpPending);
}

TEST(GateCheckTest, ALaterLevelWaitKeepsTheEarlierPendingClear)
{
    auto first = Entry;
    first.level = 10;
    const GateTable table(GateTableBytes({{1, first}, {2, Entry}, {3, Entry}}));
    auto state = Ready();
    state.level = 19;
    state.warpPending = true;

    const auto decision = GateCheck(table, state);

    ExpectWarning(decision, 350, 50, std::uint16_t{20});
    EXPECT_TRUE(decision.clearWarpPending);
}
} // namespace
