#include "World/KalimaObjectMovement.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <limits>

TEST(KalimaObjectMovementTest, UnsignedActionWindowControlsDampingAndRepulsion)
{
    constexpr std::array<std::uint8_t, 6> Actions{0x0c, 0x0d, 0x15, 0x21, 0x22, 0xff};
    for (const auto action : Actions)
    {
        Client::World::WorldObject object;
        object.transform.position = {100, 200, 300};
        object.copiedAngles = {10, 20, 30};
        Client::World::MoveKalimaObject(object, {110, 220, 999}, action);
        const bool moves = action >= 0x0d && action <= 0x21;
        EXPECT_FLOAT_EQ(object.transform.position.x, moves ? 96.4f : 100);
        EXPECT_FLOAT_EQ(object.transform.position.y, moves ? 192.8f : 200);
        EXPECT_EQ(object.transform.position.z, 300);
        EXPECT_FLOAT_EQ(object.transform.angles.x, moves ? 6 : 0);
        EXPECT_FLOAT_EQ(object.transform.angles.y, moves ? 12 : 0);
        EXPECT_FLOAT_EQ(object.transform.angles.z, moves ? 18 : 0);
    }
}
TEST(KalimaObjectMovementTest, PositiveVelocityThresholdDoesNotUseMagnitudeAndUnorderedTakesRepulsion)
{
    Client::World::WorldObject object;
    object.velocity = {0.1f, -10, 5};
    Client::World::MoveKalimaObject(object, {1, 2, 0}, 0x15);
    EXPECT_FLOAT_EQ(object.velocity.x, 0.06f);
    EXPECT_FLOAT_EQ(object.velocity.y, -6);
    EXPECT_FLOAT_EQ(object.velocity.z, 3);
    object = {};
    object.velocity.x = std::numeric_limits<float>::quiet_NaN();
    Client::World::MoveKalimaObject(object, {10, 20, 0}, 0x15);
    EXPECT_FLOAT_EQ(object.transform.position.x, -3.6f);
    EXPECT_FLOAT_EQ(object.transform.position.y, -7.2f);
    EXPECT_EQ(object.velocity.z, 0);
}
