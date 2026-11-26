#include "Pathfinding/PathFinder.h"

#include "Pathfinding/Path.h"
#include "Pathfinding/PathCell.h"
#include "Pathfinding/TerrainWalls.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace
{
using Client::Pathfinding::Path;
using Client::Pathfinding::PathCell;
using Client::Pathfinding::PathFinder;
using Client::Pathfinding::TerrainWalls;

void ExpectCells(const std::optional<Path>& path, std::initializer_list<PathCell> expected)
{
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, expected.size());
    std::size_t i = 0;
    for (const PathCell cell : expected)
    {
        EXPECT_EQ(path->cells[i].x, cell.x) << i;
        EXPECT_EQ(path->cells[i].y, cell.y) << i;
        ++i;
    }
}

// A one-row corridor: blocks the shortcuts and makes the second pass observable.
void MakeCorridor(TerrainWalls& walls)
{
    walls.cells.fill(4);
    for (std::size_t x = 90; x <= 110; ++x)
    {
        walls.cells[100 * 256 + x] = 0;
    }
    walls.cells[100 * 256 + 100] = 2;
}

TEST(PathFinderTest, CrossesTheDiagonalEvenWhenBothSideCellsAreWalls)
{
    Client::Pathfinding::TerrainWalls walls;
    walls.cells.fill(4);
    walls.cells[10 * 256 + 10] = 0;
    walls.cells[11 * 256 + 11] = 0;
    Client::Pathfinding::PathFinder finder(walls);
    const auto path = finder.Find({10, 10}, {11, 11});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 2);
    EXPECT_EQ(path->cells[0], (Client::Pathfinding::PathCell{10, 10}));
    EXPECT_EQ(path->cells[1], (Client::Pathfinding::PathCell{11, 11}));
    EXPECT_EQ(path->current, 0);
    EXPECT_EQ(path->progress, 0);
}

TEST(PathFinderTest, TheStartOnRowOrColumnZeroFails)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({0, 5}, {3, 5}));
    EXPECT_FALSE(finder.Find({5, 0}, {5, 3}));
    EXPECT_FALSE(finder.Find({0, 0}, {1, 1}));
}

TEST(PathFinderTest, ClickingTheStartDoesNotMakeAWalk)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {100, 100}));
}

TEST(PathFinderTest, EveryAdjacentGoalIsOneStep)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    constexpr std::array<PathCell, 8> goals{
        {{99, 99}, {100, 99}, {101, 99}, {99, 100}, {101, 100}, {99, 101}, {100, 101}, {101, 101}}};
    for (const PathCell goal : goals)
    {
        ExpectCells(finder.Find({100, 100}, goal), {{100, 100}, goal});
    }
}

// 0x4eb1c5 keeps the first closed parent with the minimum cost, in 0x6d5d80's order.
TEST(PathFinderTest, EqualClosedParentCostsKeepTheFirstNeighbor)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {102, 101}), {{100, 100}, {101, 100}, {102, 101}});
    ExpectCells(finder.Find({100, 100}, {101, 102}), {{100, 100}, {100, 101}, {101, 102}});
    ExpectCells(finder.Find({100, 100}, {98, 99}), {{100, 100}, {99, 99}, {98, 99}});
}

// 0x4eb2b9..0x4eb2cd and 0x4eb3ac..0x4eb3bf: h(1,1)=h(1,0)=4. The first minimum is (104,99). On close, the cost ties
// pick row y=99 again by the first closed neighbor.
TEST(PathFinderTest, AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 105] = 2;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {105, 100}), {{100, 100}, {101, 99}, {102, 99}, {103, 99}, {104, 99}});
}

TEST(PathFinderTest, AnAdjacentOccupiedGoalDoesNotDisplaceAnEquallyNearStart)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 101] = 2;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
}

TEST(PathFinderTest, InTheSafeZoneTheSecondTryWalksThroughCharacters)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100},
                                                     {96, 100},
                                                     {97, 100},
                                                     {98, 100},
                                                     {99, 100},
                                                     {100, 100},
                                                     {101, 100},
                                                     {102, 100},
                                                     {103, 100},
                                                     {104, 100},
                                                     {105, 100}});
    // Only the goal needs to be safe; the rest of the corridor does not change.
    walls.cells[100 * 256 + 95] = 0;
    walls.cells[100 * 256 + 105] = 1;
    const auto path = finder.Find({95, 100}, {105, 100});
    ASSERT_TRUE(path);
    EXPECT_EQ(path->count, 11);
    EXPECT_EQ(path->cells[10], (PathCell{105, 100}));
}

// 0x4eacbe..0x4eacc9 uses iWall=4; the occupied safe cell is 3 and must be crossable.
TEST(PathFinderTest, TheSafeZoneSecondTryAdmitsTheSafeCharacterWordThree)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    for (std::size_t x = 90; x <= 110; ++x)
    {
        walls.cells[100 * 256 + x] = 1;
    }
    walls.cells[100 * 256 + 100] = 3;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100},
                                                     {96, 100},
                                                     {97, 100},
                                                     {98, 100},
                                                     {99, 100},
                                                     {100, 100},
                                                     {101, 100},
                                                     {102, 100},
                                                     {103, 100},
                                                     {104, 100},
                                                     {105, 100}});
}

TEST(PathFinderTest, OutsideTheSafeZoneCharactersStopTheNearestPath)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, AnOccupiedGoalDisablesWalkingThroughCharactersEvenFromTheSafeZone)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    walls.cells[100 * 256 + 105] = 3;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, SafeZoneDoesNotAdmitWallFour)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    walls.cells[100 * 256 + 100] = 4;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, OnlyTheFirstFifteenCellsAreKept)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    const auto path = finder.Find({100, 100}, {120, 100});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    for (std::uint8_t i = 0; i < 15; ++i)
    {
        EXPECT_EQ(path->cells[i], (PathCell{static_cast<std::uint8_t>(100 + i), 100}));
    }
}

// 06 § Caminar: 30 in a straight line is reached in 500 pops; 40 exhausts that pass. The second pass's 50 leave ten
// steps, so the result tells both limits apart from the cut at 15.
TEST(PathFinderTest, TheExactSearchGivesUpAfter500AndTheNearestAfter50)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    const auto exact = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(exact);
    ASSERT_EQ(exact->count, 15);
    EXPECT_EQ(exact->cells[14], (PathCell{114, 100}));
    const auto nearest = finder.Find({100, 100}, {140, 100});
    ASSERT_TRUE(nearest);
    ASSERT_EQ(nearest->count, 11);
    EXPECT_EQ(nearest->cells[10], (PathCell{110, 100}));
}

TEST(PathFinderTest, NeighborFlagsIgnoreOnlyTheThreeHighBitsOfTheLowByte)
{
    TerrainWalls walls;
    walls.cells.fill(4);
    walls.cells[100 * 256 + 100] = 0;
    walls.cells[100 * 256 + 101] = 0xe0;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
    walls.cells[100 * 256 + 101] = 0x120;
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
    walls.cells[100 * 256 + 101] = 0x22;
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
}

TEST(PathFinderTest, TheExactGoalCheckUsesTheRawWordAndOnlyBitTwentyBypassesIt)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    // 0x40 lets neighbors through, but the exact goal check rejects it before searching.
    walls.cells[100 * 256 + 130] = 0x40;
    const auto nearest = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->count, 11);
    walls.cells[100 * 256 + 130] = 0x60;
    const auto exact = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(exact);
    EXPECT_EQ(exact->count, 15);
}

// 0x4eb176..0x4eb18a (close) and 0x4eb22a..0x4eb247 (open) check x and y separately; the open's linear index is only
// computed at 0x4eb24d. No wrap between rows.
TEST(PathFinderTest, TheGoalMayBeOnRowOrColumnZeroWithoutWrappingNeighbors)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    ExpectCells(finder.Find({1, 1}, {0, 0}), {{1, 1}, {0, 0}});
    ExpectCells(finder.Find({254, 254}, {255, 255}), {{254, 254}, {255, 255}});
    walls.cells.fill(4);
    walls.cells[100 * 256 + 255] = 0;
    walls.cells[100 * 256] = 0;
    EXPECT_FALSE(finder.Find({255, 100}, {0, 100}));
}

// 0x4eae0a..0x4eae0d clears before 0x4eae16..0x4eae38: the first pass reaches the goal. If it checked WORD 2 first, it
// would fall to the nearest of 50 pops and give only 11 cells.
TEST(PathFinderTest, ClearGoalRunsBeforeTheExactGoalCheck)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 130] = 2;
    PathFinder finder(walls);
    const auto path = finder.Find({100, 100}, {130, 100}, true);
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{114, 100}));
    EXPECT_EQ(walls.cells[100 * 256 + 130], 0);
}

TEST(PathFinderTest, ClearGoalWritesTheWholeWordEvenIfTheWalkFails)
{
    TerrainWalls walls;
    walls.cells.fill(0xffff);
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {105, 100}, true));
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0);
    EXPECT_EQ(walls.cells[100 * 256 + 104], 0xffff);
    // The early exit on sx/sy at 0 happens before clearGoal.
    walls.cells[100 * 256 + 105] = 0x106;
    EXPECT_FALSE(finder.Find({0, 100}, {105, 100}, true));
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0x106);
}

// 0x4eae0d clears the goal before 0x4eac7a..0x4eacce checks the safe zone again.
TEST(PathFinderTest, ClearingTheOnlySafeCellBeforeTheSecondTryDoesNotAllowCrossingCharacters)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 105] = 1;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}, true), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0);
}

// The original clears the inclusive range on entry (0x4ead7b..0x4eadbe). Goal, start and neighbors widen it: stores
// 0x4eae5f/0x4eae71, 0x4eb0b9/0x4eb0cb and 0x4eb342/0x4eb35d. The goal joins the range even if it is never opened:
// its GoalFlag does not stay for another call either.
TEST(PathFinderTest, TheStartOccupancyDoesNotPreventWalkingAndCallsDoNotRetainSearchFlags)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 100] = 2;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
    EXPECT_FALSE(finder.Find({100, 100}, {100, 100}));
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
}

// A U path without shortcuts: the base's ends stay walls, and only the middle vertex joins the columns through two
// diagonals. Each open cell has a single neighbor forward.
void MakeU(TerrainWalls& walls, std::size_t bottom, std::size_t rightTop)
{
    walls.cells.fill(4);
    for (std::size_t y = 5; y < bottom; ++y)
    {
        walls.cells[y * 256 + 10] = 0;
    }
    walls.cells[bottom * 256 + 11] = 0;
    for (std::size_t y = rightTop; y < bottom; ++y)
    {
        walls.cells[y * 256 + 12] = 0;
    }
}

TEST(PathFinderTest, TheFiveHundredthExtractionAndReconstructionCellStillSucceed)
{
    TerrainWalls walls;
    // 249 on the left + vertex + 250 on the right = 500, including the goal (12,4).
    MakeU(walls, 254, 4);
    PathFinder finder(walls);
    const auto path = finder.Find({10, 5}, {12, 4});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{10, 19}));
    // A dead branch with h=13 pops before the corridor runs out. The goal would be the 501st. In the second pass the
    // first 50 do not improve h(start)=9, so it returns a single cell.
    walls.cells[5 * 256 + 9] = 0;
    EXPECT_FALSE(finder.Find({10, 5}, {12, 4}));
}

TEST(PathFinderTest, TheFiftiethExtractionMayImproveTheNearestButTheFiftyFirstCannot)
{
    TerrainWalls walls;
    // Occupied goal: straight to the second pass. 25 on the left + vertex + 24 on the right up to (12,6) = 50.
    // h(12,6)=8 is the first below h(start)=9.
    MakeU(walls, 30, 5);
    walls.cells[4 * 256 + 12] = 2;
    PathFinder finder(walls);
    const auto path = finder.Find({10, 5}, {12, 4});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{10, 19}));
    // With one more dead pop, (12,6) would be the 51st: none of the 50 improves the start.
    walls.cells[5 * 256 + 9] = 0;
    EXPECT_FALSE(finder.Find({10, 5}, {12, 4}));
}

// 0x4eae7c..0x4eb054 (the elf that walks up, 0x566271): with a distance, the goal is the cells closer than it. With
// 1.8 (r = 1) the 3 × 3 square, since √2 < 1.8: the path stops one cell before the target. With 2.0 (r = 2) the
// diamond |dx| + |dy| ≤ 2, without (±2, ±1) (√5 ≥ 2): two before. The exact pass does not mark wall cells, and with a
// distance clearGoal does not count.
TEST(PathFinderTest, TheDistanceGoalStopsWithinTheRange)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    // With 1.8 the integer radius is 1, but the corners (104, 99) and (104, 101) are goals too (√2 < 1.8): the search
    // reaches the upper one first.
    ExpectCells(finder.Find({100, 100}, {105, 100}, false, 1.8f),
                {{100, 100}, {101, 99}, {102, 99}, {103, 99}, {104, 99}});
    ExpectCells(finder.Find({100, 100}, {105, 100}, false, 2.0f), {{100, 100}, {101, 100}, {102, 100}, {103, 100}});
    // Already within the distance: no two-cell path.
    EXPECT_FALSE(finder.Find({104, 100}, {105, 100}, false, 1.8f));
    walls.cells[100 * 256 + 103] = 4;
    walls.cells[100 * 256 + 105] = 4;
    const auto around = finder.Find({100, 100}, {105, 100}, true, 2.0f);
    ASSERT_TRUE(around);
    EXPECT_EQ(walls.cells[100 * 256 + 105], 4);
    const PathCell last = around->cells[around->count - 1];
    EXPECT_NE(last, (PathCell{103, 100}));
    EXPECT_LE(std::abs(last.x - 105) + std::abs(last.y - 100), 2);
}

// The goal cells use the linear index: near the right edge, the next row.
TEST(PathFinderTest, TheDistanceGoalWrapsIntoTheNextRow)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    // r = 1 around (255, 50): (256, 50) is (0, 51).
    const auto path = finder.Find({3, 51}, {255, 50}, false, 1.0f);
    ASSERT_TRUE(path);
    const PathCell last = path->cells[path->count - 1];
    EXPECT_EQ(last, (PathCell{0, 51}));
}
} // namespace
