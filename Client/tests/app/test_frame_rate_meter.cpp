#include "App/FrameRateMeter.h"

#include "App/GameLoop.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

// Client::App::FrameRateMeter: los promedios de --stats, de a StatsInterval.
namespace
{
using namespace std::chrono_literals;
using Client::App::FrameRateMeter;
using Client::App::FrameStats;
using Client::App::StatsInterval;

FrameStats StatsOf(std::uint64_t frames, std::uint64_t steps)
{
    FrameStats stats;
    stats.frames = frames;
    stats.steps = steps;
    return stats;
}

TEST(FrameRateMeterTest, GivesNothingBeforeTheInterval)
{
    FrameRateMeter meter{1s};

    EXPECT_FALSE(meter.Update(1s + StatsInterval - 1ns, StatsOf(100, 125)).has_value());
}

// En 5 s, 100 frames y 125 pasos: 20 FPS y 25 pasos por segundo.
TEST(FrameRateMeterTest, AveragesTheFramesAndStepsOfTheInterval)
{
    FrameRateMeter meter{1s};

    const auto rates = meter.Update(1s + StatsInterval, StatsOf(100, 125));

    ASSERT_TRUE(rates.has_value());
    EXPECT_DOUBLE_EQ(rates->framesPerSecond, 20.0);
    EXPECT_DOUBLE_EQ(rates->stepsPerSecond, 25.0);
}

// El segundo promedio cuenta desde el primero: solo los frames y los pasos de su tramo, aunque el
// tramo dure más que StatsInterval.
TEST(FrameRateMeterTest, TheNextAverageStartsFromTheLastOne)
{
    FrameRateMeter meter{0s};
    ASSERT_TRUE(meter.Update(5s, StatsOf(100, 125)).has_value());

    EXPECT_FALSE(meter.Update(9s, StatsOf(180, 225)).has_value());
    const auto rates = meter.Update(15s, StatsOf(400, 375));

    ASSERT_TRUE(rates.has_value());
    EXPECT_DOUBLE_EQ(rates->framesPerSecond, 30.0);
    EXPECT_DOUBLE_EQ(rates->stepsPerSecond, 25.0);
}

TEST(FrameRateMeterTest, AClockThatGoesBackGivesNothing)
{
    FrameRateMeter meter{10s};

    EXPECT_FALSE(meter.Update(4s, StatsOf(100, 125)).has_value());
}
} // namespace
