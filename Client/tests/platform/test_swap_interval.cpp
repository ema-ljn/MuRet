#include "Platform/Sdl/SwapInterval.h"

#include "Platform/VSyncMode.h"

#include <gtest/gtest.h>

namespace
{
using Client::Platform::VSyncMode;
using Client::Platform::Sdl::ApplySwapInterval;
using Client::Platform::Sdl::SwapIntervalFor;
using Client::Platform::Sdl::SwapIntervalResult;

// A driver without adaptive: only 1 sticks.
bool SetOnlyOn(int interval)
{
    return interval == 1;
}

// A driver that lets nothing change (Xvfb's llvmpipe).
bool SetNothing(int /*interval*/)
{
    return false;
}

bool SetEverything(int /*interval*/)
{
    return true;
}

// The context was left with no wait.
bool GetZero(int* interval)
{
    *interval = 0;
    return true;
}

bool GetFails(int* /*interval*/)
{
    return false;
}

TEST(SwapIntervalTest, MapsEachMode)
{
    EXPECT_EQ(SwapIntervalFor(VSyncMode::On), 1);
    EXPECT_EQ(SwapIntervalFor(VSyncMode::Adaptive), -1);
    EXPECT_EQ(SwapIntervalFor(VSyncMode::Off), 0);
}

TEST(SwapIntervalTest, TheWantedModeStaysWhenItWorks)
{
    const SwapIntervalResult result = ApplySwapInterval(VSyncMode::Adaptive, SetEverything, GetFails);

    EXPECT_EQ(result.mode, VSyncMode::Adaptive);
    EXPECT_TRUE(result.error.empty());
}

TEST(SwapIntervalTest, AdaptiveFallsBackToOn)
{
    const SwapIntervalResult result = ApplySwapInterval(VSyncMode::Adaptive, SetOnlyOn, GetFails);

    EXPECT_EQ(result.mode, VSyncMode::On);
}

TEST(SwapIntervalTest, OnFallsBackToWhatTheContextHas)
{
    const SwapIntervalResult result = ApplySwapInterval(VSyncMode::On, SetNothing, GetZero);

    EXPECT_EQ(result.mode, VSyncMode::Off);
}

TEST(SwapIntervalTest, AFailedGetMeansOff)
{
    const SwapIntervalResult result = ApplySwapInterval(VSyncMode::Adaptive, SetNothing, GetFails);

    EXPECT_EQ(result.mode, VSyncMode::Off);
}

TEST(SwapIntervalTest, OffThatStaysOffHasNoError)
{
    const SwapIntervalResult result = ApplySwapInterval(VSyncMode::Off, SetNothing, GetZero);

    EXPECT_EQ(result.mode, VSyncMode::Off);
    EXPECT_TRUE(result.error.empty());
}
} // namespace
