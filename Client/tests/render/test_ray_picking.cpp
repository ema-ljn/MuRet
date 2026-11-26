#include "Models/RayPicking.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <optional>

// The mouse segment against each character's box, like 0x622900 (a separation on 6 axes) and 0x59e700 (the smallest
// distance² from the camera to its origin wins), with the shifted player box of Q-C100 (06 § El picking).
namespace
{
using Client::Models::Box;
using Client::Models::NearestHit;
using Client::Models::PlayerBoxAt;
using Client::Models::SegmentHitsBox;

constexpr Box UnitBox{{-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f}};

TEST(RayPickingTest, HitsTheBoxInFront)
{
    EXPECT_TRUE(SegmentHitsBox({0, 0, 100}, {0, 0, -100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({-100, -100, -100}, {100, 100, 100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({4, -100, 0}, {4, 100, 0}, UnitBox));
}

TEST(RayPickingTest, MissesBeside)
{
    EXPECT_FALSE(SegmentHitsBox({6, 0, 100}, {6, 0, -100}, UnitBox));
    EXPECT_FALSE(SegmentHitsBox({-100, 6, -100}, {100, 6, 100}, UnitBox));
    // Diagonally, beside an edge: only a cross-product axis separates it.
    EXPECT_FALSE(SegmentHitsBox({-100, 112, 0}, {100, -88, 0}, UnitBox));
}

// It is a segment: what lies past its end, or before its origin, is not touched.
TEST(RayPickingTest, TheSegmentEndsWhereItEnds)
{
    EXPECT_FALSE(SegmentHitsBox({0, 0, 100}, {0, 0, 10}, UnitBox));
    EXPECT_FALSE(SegmentHitsBox({0, 0, -10}, {0, 0, -100}, UnitBox));
    EXPECT_TRUE(SegmentHitsBox({0, 0, 100}, {0, 0, 4}, UnitBox));
}

// Q-C100: the 0x20a box goes from (−60, −60, 0) to (40, 40, 120) from the position, without rotating with the
// character.
TEST(RayPickingTest, ThePlayerBoxIsOffCentre)
{
    const glm::vec3 position{1000.0f, 2000.0f, 163.0f};
    const Box box = PlayerBoxAt(position);

    EXPECT_EQ(box.min, glm::vec3(940.0f, 1940.0f, 163.0f));
    EXPECT_EQ(box.max, glm::vec3(1040.0f, 2040.0f, 283.0f));
    // A vertical segment at x + 45 passes beside it, and one at x − 55, inside.
    EXPECT_FALSE(SegmentHitsBox(position + glm::vec3(45, 0, 500), position + glm::vec3(45, 0, -500), box));
    EXPECT_TRUE(SegmentHitsBox(position + glm::vec3(-55, 0, 500), position + glm::vec3(-55, 0, -500), box));
}

// 0x59e700: of those the segment touches, the one with the smallest distance² from the camera (the origin) to its
// position, not to the point where it is touched. With none, std::nullopt.
TEST(RayPickingTest, TheNearestIsByOriginDistance)
{
    const glm::vec3 camera{0.0f, 0.0f, 1000.0f};
    const glm::vec3 end{0.0f, 0.0f, -1000.0f};
    // 0 and 2 are below the camera; 2 closer. 1, beside it.
    const std::array<glm::vec3, 3> positions = {glm::vec3{10, 10, 0}, glm::vec3{500, 0, 0}, glm::vec3{10, 10, 300}};

    EXPECT_EQ(NearestHit(camera, end, positions), std::optional<std::size_t>{2});
    EXPECT_EQ(NearestHit(camera, end, std::array<glm::vec3, 1>{glm::vec3{500, 0, 0}}), std::nullopt);
}

// The nearest origin wins even if the segment enters another box first. The camera looks along x at height 60: it
// enters B's box (x from 0 to 100, z from −59 to 61) right away, and A's (x from 40 to 140) further on, but A's origin
// is 100² + 60² from the camera and B's 60² + 119².
TEST(RayPickingTest, TheNearestIsNotWhereTheSegmentEnters)
{
    const glm::vec3 camera{0.0f, 0.0f, 60.0f};
    const glm::vec3 end{2000.0f, 0.0f, 60.0f};
    const std::array<glm::vec3, 2> positions = {glm::vec3{60, 0, -59}, glm::vec3{100, 0, 0}};

    EXPECT_EQ(NearestHit(camera, end, positions), std::optional<std::size_t>{1});
}
} // namespace
