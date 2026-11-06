#include "GameLogic/GateCheck.h"

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "GameLogic/GateCheckState.h"
#include "GameLogic/GateDecision.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

// Gate.bmd real, descifrado por el cargador de CL1 igual que scripts/game/maps/gate.py.
namespace
{
using Client::Data::Tables::GateRecord;
using Client::Data::Tables::LoadGateTable;
using Client::Data::Tables::ReadGateRecord;
using Client::GameLogic::GateCheck;
using Client::GameLogic::GateCheckState;
using Client::Tests::ReadFileBytes;

using GoldenGateCheck = Client::Tests::GoldenDataTest;

TEST_F(GoldenGateCheck, TheFourLorenciaEntriesUseTheirExactRectangleAndLevel)
{
    const auto path = DataFile("Gate.bmd");
    ASSERT_TRUE(path.has_value());
    const auto file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = LoadGateTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());
    const auto& table = loaded.Value();
    const std::array<std::pair<std::size_t, GateRecord>, 4> gates{{
        {1, {1, 0, 121, 232, 123, 233, 2, 0, 20}},
        {18, {1, 0, 5, 38, 6, 41, 19, 0, 15}},
        {23, {1, 0, 213, 246, 217, 247, 24, 0, 10}},
        {102, {1, 0, 239, 14, 240, 15, 103, 0, 10}},
    }};
    for (const auto& [index, gate] : gates)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(ReadGateRecord(table, index), gate);
        GateCheckState state{
            .x = (gate.x1 + gate.x2) / 2, .y = (gate.y1 + gate.y2) / 2, .level = gate.level, .elapsedMs = 3000};
        EXPECT_EQ(GateCheck(table, state).request, index);
        for (const std::int32_t x : {gate.x1, gate.x2})
        {
            for (const std::int32_t y : {gate.y1, gate.y2})
            {
                state.x = x;
                state.y = y;
                EXPECT_EQ(GateCheck(table, state).request, index);
            }
        }
        state.level = static_cast<std::uint16_t>(gate.level - 1);
        const auto low = GateCheck(table, state);
        EXPECT_EQ(low.request, std::nullopt);
        ASSERT_EQ(low.warnings.size(), 1);
        EXPECT_EQ(low.warnings[0].textId, 350);
        EXPECT_EQ(low.warnings[0].requiredLevel, gate.level);
        EXPECT_EQ(low.waitSteps, 50);
        state.level = gate.level;
        for (const std::int32_t x : {gate.x1 - 1, gate.x2 + 1})
        {
            state.x = x;
            state.y = gate.y1;
            EXPECT_EQ(GateCheck(table, state).request, std::nullopt);
        }
        state.x = gate.x1;
        for (const std::int32_t y : {gate.y1 - 1, gate.y2 + 1})
        {
            state.y = y;
            EXPECT_EQ(GateCheck(table, state).request, std::nullopt);
        }
    }
}

TEST_F(GoldenGateCheck, TheDestinationOfGate102KeepsItsReversedYCoordinates)
{
    const auto path = DataFile("Gate.bmd");
    ASSERT_TRUE(path.has_value());
    const auto file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());
    const auto loaded = LoadGateTable(std::as_bytes(std::span(*file)));
    ASSERT_TRUE(loaded.HasValue());

    EXPECT_EQ(ReadGateRecord(loaded.Value(), 103), (GateRecord{2, 30, 29, 42, 30, 37, 0, 0, 10}));
}
} // namespace
