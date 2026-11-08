#include "GameLogic/MovePath.h"
#include "GameLogic/Path.h"
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <vector>
namespace
{
struct MovingState
{
    Client::GameLogic::Path path{0, 0, 3, {{{0, 0}, {1, 0}, {2, 0}}}};
    glm::vec3 position{50.0f, 50.0f, 77.0f};
    float yaw = 0.0f;
    std::uint32_t logicalX = 0;
    std::uint32_t logicalY = 0;
    Client::GameLogic::MovePathState View()
    {
        return {path, position, yaw, logicalX, logicalY};
    }
};
TEST(MovePathTest, AdvancesOnlyOneQuarterWithoutMovingPosition)
{
    MovingState actor;
    actor.position.x = 67.96875f;
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
    EXPECT_EQ(actor.path.progress, 1);
    EXPECT_EQ(actor.path.current, 0);
    EXPECT_EQ(actor.logicalX, 1u);
    EXPECT_EQ(actor.position.x, 67.96875f);
    EXPECT_EQ(actor.position.z, 77.0f);
}
TEST(MovePathTest, AdvancesTheSegmentBeforeAdvancingItsLogicalCellNextCall)
{
    MovingState actor;
    actor.path.progress = 3;
    actor.logicalX = 1;
    actor.position.x = 150.0f;
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
    EXPECT_EQ(actor.path.current, 1);
    EXPECT_EQ(actor.path.progress, 0);
    EXPECT_EQ(actor.logicalX, 1u);
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
    EXPECT_EQ(actor.logicalX, 2u);
}
TEST(MovePathTest, ArrivalSnapsAtDistanceTwentyAndSkipsHeading)
{
    MovingState actor;
    actor.path.current = 1;
    actor.path.progress = 3;
    actor.position.x = 230.0f;
    actor.yaw = 123.0f;
    EXPECT_TRUE(Client::GameLogic::MovePath(actor.View(), 1));
    EXPECT_EQ(actor.path.current, 2);
    EXPECT_EQ(actor.path.progress, 0);
    EXPECT_EQ(actor.position.x, 250.0f);
    EXPECT_EQ(actor.position.y, 50.0f);
    EXPECT_EQ(actor.yaw, 123.0f);
    EXPECT_EQ(actor.logicalX, 2u);
}
TEST(MovePathTest, EmptyOrConsumedPathHasNoEffects)
{
    MovingState actor;
    actor.path.current = actor.path.count;
    int calls = 0;
    const Client::GameLogic::LogicalCellEffects effects{[&](auto, auto, auto&) { ++calls; }};
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 1, effects));
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(actor.logicalX, 0u);
    EXPECT_EQ(actor.yaw, 0.0f);
}
TEST(MovePathTest, CallsEffectsAroundBothStoresEvenForTheSameCellAndRereadsThePath)
{
    using namespace Client::GameLogic;
    MovingState actor;
    actor.logicalX = 1;
    std::vector<int> events;
    const LogicalCellEffects effects{[&](LogicalAxis axis, LogicalStoreStage stage, std::uint32_t& cell)
                                     {
                                         events.push_back((axis == LogicalAxis::X ? 0 : 10) + static_cast<int>(stage));
                                         if (axis == LogicalAxis::X && stage == LogicalStoreStage::BeginningBefore)
                                         {
                                             EXPECT_EQ(cell, 1u);
                                             actor.path.cells[1].x = 2;
                                         }
                                         if (axis == LogicalAxis::X && stage == LogicalStoreStage::BeginningAfter)
                                         {
                                             EXPECT_EQ(cell, 2u);
                                         }
                                     }};
    EXPECT_FALSE(MovePath(actor.View(), 0, effects));
    EXPECT_EQ(events, (std::vector<int>{0, 1, 10, 11}));
    EXPECT_EQ(actor.logicalX, 2u);
}
TEST(MovePathTest, QuarterTargetsUseClampedEndpointsAndLiteralWeights)
{
    constexpr std::array Targets{67.96875f, 93.75f, 122.65625f, 150.0f};
    for (std::uint8_t q = 0; q < Targets.size(); ++q)
    {
        MovingState actor;
        actor.path.progress = q;
        actor.position.x = Targets[q] - 20.0f;
        EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
        EXPECT_EQ(actor.path.progress, q == 3 ? 0 : q + 1);
        actor = MovingState{};
        actor.path.progress = q;
        actor.position.x = Targets[q] - 20.0001f;
        EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
        EXPECT_EQ(actor.path.progress, q);
    }
}
TEST(MovePathTest, HeadingSnapsFromFortyFiveAndOtherwiseTurnsByHalfDistance)
{
    MovingState actor;
    actor.position = {0.0f, 50.0f, 0.0f};
    actor.yaw = 45.0f;
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 1));
    EXPECT_EQ(actor.yaw, 90.0f);
    actor.yaw = 60.0f;
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 2));
    EXPECT_EQ(actor.yaw, 75.0f);
}
TEST(MovePathTest, ArrivalEffectsSeeCountersBeforeThePositionSnap)
{
    using namespace Client::GameLogic;
    MovingState actor;
    actor.path.current = 1;
    actor.path.progress = 3;
    actor.position.x = 230.0f;
    std::vector<int> events;
    const LogicalCellEffects effects{[&](LogicalAxis axis, LogicalStoreStage stage, auto&)
                                     {
                                         events.push_back((axis == LogicalAxis::X ? 0 : 10) + static_cast<int>(stage));
                                         EXPECT_EQ(actor.path.current, 2);
                                         EXPECT_EQ(actor.path.progress, 0);
                                         EXPECT_EQ(actor.position.x, 230.0f);
                                     }};
    EXPECT_TRUE(MovePath(actor.View(), 1, effects));
    EXPECT_EQ(events, (std::vector<int>{2, 3, 12, 13}));
}
TEST(MovePathTest, HeadingUsesTheOldTargetInTheFrameThatAdvancesTheSegment)
{
    MovingState actor;
    actor.path.progress = 3;
    actor.path.cells[2] = {1, 1};
    actor.position = {150.0f, 30.0f, 77.0f};
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 1));
    EXPECT_EQ(actor.path.current, 1);
    EXPECT_EQ(actor.yaw, 180.0f);
    EXPECT_EQ(actor.position.y, 30.0f);
}
TEST(MovePathTest, SingleCellRouteClampsAllFourControlPoints)
{
    MovingState actor;
    actor.path.count = 1;
    actor.path.progress = 2;
    EXPECT_FALSE(Client::GameLogic::MovePath(actor.View(), 0));
    EXPECT_EQ(actor.path.progress, 3);
    EXPECT_TRUE(Client::GameLogic::MovePath(actor.View(), 0));
    EXPECT_EQ(actor.path.current, 0);
    EXPECT_EQ(actor.path.progress, 0);
    EXPECT_EQ(actor.position.x, 50.0f);
}
TEST(MovePathTest, ArrivalRereadsCurrentAfterEachPreCallbackAndKeepsTheOriginalSnap)
{
    using namespace Client::GameLogic;
    MovingState actor;
    actor.path.cells[0] = {3, 4};
    actor.path.cells[1] = {5, 6};
    actor.path.cells[2] = {7, 8};
    actor.path.current = 1;
    actor.path.progress = 3;
    actor.position = {730.0f, 850.0f, 77.0f};
    actor.yaw = 123.0f;
    std::vector<int> events;
    const LogicalCellEffects effects{[&](LogicalAxis axis, LogicalStoreStage stage, std::uint32_t& cell)
                                     {
                                         events.push_back((axis == LogicalAxis::X ? 0 : 10) + static_cast<int>(stage));
                                         EXPECT_EQ(actor.path.progress, 0);
                                         EXPECT_EQ(actor.position.x, 730.0f);
                                         EXPECT_EQ(actor.position.y, 850.0f);
                                         if (axis == LogicalAxis::X && stage == LogicalStoreStage::ArrivalBefore)
                                         {
                                             EXPECT_EQ(actor.path.current, 2);
                                             actor.path.current = 0;
                                         }
                                         if (axis == LogicalAxis::X && stage == LogicalStoreStage::ArrivalAfter)
                                         {
                                             EXPECT_EQ(cell, 3u);
                                             EXPECT_EQ(actor.path.current, 0);
                                         }
                                         if (axis == LogicalAxis::Y && stage == LogicalStoreStage::ArrivalBefore)
                                         {
                                             EXPECT_EQ(actor.path.current, 0);
                                             actor.path.current = 1;
                                         }
                                         if (axis == LogicalAxis::Y && stage == LogicalStoreStage::ArrivalAfter)
                                         {
                                             EXPECT_EQ(cell, 6u);
                                             EXPECT_EQ(actor.path.current, 1);
                                         }
                                     }};
    EXPECT_TRUE(MovePath(actor.View(), 1, effects));
    EXPECT_EQ(events, (std::vector<int>{2, 3, 12, 13}));
    EXPECT_EQ(actor.logicalX, 3u);
    EXPECT_EQ(actor.logicalY, 6u);
    EXPECT_EQ(actor.path.current, 1);
    EXPECT_EQ(actor.path.progress, 0);
    EXPECT_EQ(actor.position, (glm::vec3{750.0f, 850.0f, 77.0f}));
    EXPECT_EQ(actor.yaw, 123.0f);
}
TEST(MovePathTest, BeginningKeepsItsCapturedNextIndexWhenPreCallbacksChangeCurrent)
{
    using namespace Client::GameLogic;
    MovingState actor;
    actor.path.cells[0] = {3, 4};
    actor.path.cells[1] = {5, 6};
    actor.path.cells[2] = {7, 8};
    actor.position = {0.0f, 0.0f, 77.0f};
    const LogicalCellEffects effects{[&](LogicalAxis axis, LogicalStoreStage stage, auto&)
                                     {
                                         if (axis == LogicalAxis::X && stage == LogicalStoreStage::BeginningBefore)
                                         {
                                             actor.path.current = 2;
                                         }
                                         if (axis == LogicalAxis::Y && stage == LogicalStoreStage::BeginningBefore)
                                         {
                                             actor.path.current = 0;
                                         }
                                     }};
    EXPECT_FALSE(MovePath(actor.View(), 0, effects));
    EXPECT_EQ(actor.logicalX, 5u);
    EXPECT_EQ(actor.logicalY, 6u);
    EXPECT_EQ(actor.path.current, 0);
    EXPECT_EQ(actor.path.progress, 0);
    EXPECT_EQ(actor.position, (glm::vec3{0.0f, 0.0f, 77.0f}));
}
} // namespace
