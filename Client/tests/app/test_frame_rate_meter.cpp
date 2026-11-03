#include "App/FrameRateMeter.h"

#include "App/GameLoop.h"

#include <gtest/gtest.h>

// Por los literales (1s, 1ns), que include-cleaner 18 no asocia a <chrono>.
#include <chrono> // IWYU pragma: keep

// Client::App::FrameRateMeter: los promedios de --stats, de a StatsInterval.
namespace
{
using namespace std::chrono_literals;
using Client::App::FrameRateMeter;
using Client::App::FrameStats;
using Client::App::StatsInterval;

TEST(FrameRateMeterTest, GivesNothingBeforeTheInterval)
{
    FrameRateMeter meter{1s};

    EXPECT_FALSE(meter.Update(1s + StatsInterval - 1ns, FrameStats{.frames = 100, .steps = 125}).has_value());
}

// En 5 s, 100 frames y 125 pasos: 20 FPS y 25 pasos por segundo.
TEST(FrameRateMeterTest, AveragesTheFramesAndStepsOfTheInterval)
{
    FrameRateMeter meter{1s};

    const auto rates = meter.Update(1s + StatsInterval, FrameStats{.frames = 100, .steps = 125});

    ASSERT_TRUE(rates.has_value());
    EXPECT_DOUBLE_EQ(rates->framesPerSecond, 20.0);
    EXPECT_DOUBLE_EQ(rates->stepsPerSecond, 25.0);
}

// El segundo promedio cuenta desde el primero: solo los frames y los pasos de su tramo, aunque el
// tramo dure más que StatsInterval.
TEST(FrameRateMeterTest, TheNextAverageStartsFromTheLastOne)
{
    FrameRateMeter meter{0s};
    ASSERT_TRUE(meter.Update(5s, FrameStats{.frames = 100, .steps = 125}).has_value());

    EXPECT_FALSE(meter.Update(9s, FrameStats{.frames = 180, .steps = 225}).has_value());
    const auto rates = meter.Update(15s, FrameStats{.frames = 400, .steps = 375});

    ASSERT_TRUE(rates.has_value());
    EXPECT_DOUBLE_EQ(rates->framesPerSecond, 30.0);
    EXPECT_DOUBLE_EQ(rates->stepsPerSecond, 25.0);
}

TEST(FrameRateMeterTest, AClockThatGoesBackGivesNothing)
{
    FrameRateMeter meter{10s};

    EXPECT_FALSE(meter.Update(4s, FrameStats{.frames = 100, .steps = 125}).has_value());
}
} // namespace
