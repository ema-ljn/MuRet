#include "World/Birds.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
using Client::World::FinishOrdinaryBirdMotion;

TEST(BirdMotionTailTest, WaterTurnSubtractsOnceAndDeathStillDecrementsTheResetTimer)
{
    const std::int32_t map = 7;
    const std::uint16_t attributes = 1;
    std::uint8_t alive = 0x80;
    std::int32_t state = 1;
    std::int32_t timer = 0;
    glm::vec3 angles{11, 22, 725};
    FinishOrdinaryBirdMotion({alive, state, timer, angles}, map, attributes);
    EXPECT_EQ(angles, glm::vec3(11, 22, 545));
    EXPECT_EQ(state, 2);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(timer, 9);
}

TEST(BirdMotionTailTest, OnlyMapSevenAndExactWordOneTurnWhenTheTimerExpired)
{
    struct Case
    {
        std::int32_t map;
        std::uint16_t attributes;
        std::int32_t timer;
    };
    constexpr std::array Cases{Case{7, 0, 0}, Case{7, 2, 0}, Case{7, 0x101, 0}, Case{263, 1, 0},
                               Case{0, 1, 0}, Case{7, 1, 1}, Case{-1, 1, -5}};
    for (const Case& sample : Cases)
    {
        std::uint8_t alive = 0x80;
        std::int32_t state = 0;
        std::int32_t timer = sample.timer;
        glm::vec3 angles{11, 22, 179};
        FinishOrdinaryBirdMotion({alive, state, timer, angles}, sample.map, sample.attributes);
        EXPECT_EQ(angles.z, 179);
        EXPECT_EQ(state, 0);
        EXPECT_EQ(alive, 0x80);
        EXPECT_EQ(timer, sample.timer - 1);
    }
}

TEST(BirdMotionTailTest, AngleWrapIncludesEqualityAndKeepsUnordered)
{
    const std::int32_t map = 7;
    const std::uint16_t attributes = 1;
    struct Case
    {
        float before;
        float after;
    };
    constexpr std::array Cases{Case{179, 359}, Case{180, 0}, Case{-180, 0}, Case{-181, -1}, Case{900, 720}};
    for (const Case& sample : Cases)
    {
        std::uint8_t alive = 1;
        std::int32_t state = 0;
        std::int32_t timer = -1;
        glm::vec3 angles{0, 0, sample.before};
        FinishOrdinaryBirdMotion({alive, state, timer, angles}, map, attributes);
        EXPECT_EQ(angles.z, sample.after);
        EXPECT_EQ(alive, 1);
        EXPECT_EQ(state, 1);
        EXPECT_EQ(timer, 9);
    }
    std::uint8_t alive = 0;
    std::int32_t state = 0;
    std::int32_t timer = 0;
    glm::vec3 angles{0, 0, std::numeric_limits<float>::quiet_NaN()};
    FinishOrdinaryBirdMotion({alive, state, timer, angles}, map, attributes);
    EXPECT_TRUE(std::isnan(angles.z));
    EXPECT_EQ(state, 1);
    EXPECT_EQ(timer, 9);
    EXPECT_EQ(alive, 0) << "el tail no reactiva un actor dado de baja por un helper previo";
}

TEST(BirdMotionTailTest, StateAndTimerUseIndependentSignedTestsAndWrap32Stores)
{
    const std::int32_t map = 0;
    const std::uint16_t attributes = 0;
    std::uint8_t alive = 0x80;
    std::int32_t state = 2;
    std::int32_t timer = std::numeric_limits<std::int32_t>::min();
    glm::vec3 angles{};
    FinishOrdinaryBirdMotion({alive, state, timer, angles}, map, attributes);
    EXPECT_EQ(alive, 0);
    EXPECT_EQ(timer, 2147483647);
    const std::int32_t waterMap = 7;
    const std::uint16_t waterAttributes = 1;
    alive = 0xff;
    state = std::numeric_limits<std::int32_t>::max();
    timer = -1;
    FinishOrdinaryBirdMotion({alive, state, timer, angles}, waterMap, waterAttributes);
    EXPECT_EQ(state, -2147483647 - 1);
    EXPECT_EQ(alive, 0xff);
    EXPECT_EQ(timer, 9);
}
} // namespace
