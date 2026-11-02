#include "Platform/SwapInterval.h"

#include <gtest/gtest.h>

// Client::Platform::SwapIntervalFor: el intervalo de SDL de cada VSyncMode.
namespace
{
using Client::App::VSyncMode;
using Client::Platform::SwapIntervalFor;

TEST(SwapIntervalTest, MapsEachMode)
{
    EXPECT_EQ(SwapIntervalFor(VSyncMode::On), 1);
    EXPECT_EQ(SwapIntervalFor(VSyncMode::Adaptive), -1);
    EXPECT_EQ(SwapIntervalFor(VSyncMode::Off), 0);
}
} // namespace
