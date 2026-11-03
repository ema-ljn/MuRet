#include "Platform/SwapInterval.h"

#include "App/VSyncMode.h"

#include <gtest/gtest.h>

// Client::Platform::SwapIntervalFor y ApplySwapInterval, con dobles de SDL_GL_SetSwapInterval y
// SDL_GL_GetSwapInterval: sin ventana ni contexto.
namespace
{
using Client::App::VSyncMode;
using Client::Platform::ApplySwapInterval;
using Client::Platform::SwapIntervalFor;
using Client::Platform::SwapIntervalResult;

// Un driver sin adaptativo: solo entra el 1.
bool SetOnlyOn(int interval)
{
    return interval == 1;
}

// Un driver que no deja cambiar nada (el llvmpipe de Xvfb).
bool SetNothing(int /*interval*/)
{
    return false;
}

bool SetEverything(int /*interval*/)
{
    return true;
}

// El contexto se quedó sin espera.
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
