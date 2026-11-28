#include "App/FixedStep.h"

#include "Kernel/LoopTiming.h"

#include <gtest/gtest.h>

#include <chrono>

// Client::App::FixedStep: the normal mode's 40 ms fixed step, with its alpha and its cap.
namespace
{
using namespace std::chrono_literals;
using Client::App::FixedStep;

// The alpha tolerance the plan sets.
constexpr double AlphaTolerance = 1e-6;

TEST(FixedStepTest, FortyIsOneStep)
{
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(40ms), 1);
    EXPECT_NEAR(fixedStep.Alpha(), 0.0, AlphaTolerance);
}

TEST(FixedStepTest, ThirtyNineIsNone)
{
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(39ms), 0);
    EXPECT_NEAR(fixedStep.Alpha(), 0.975, AlphaTolerance);
}

// The 02 catch-up: a 130 ms frame runs 3 steps and leaves 10 ms.
TEST(FixedStepTest, OneHundredThirtyIsThree)
{
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(130ms), 3);
    EXPECT_NEAR(fixedStep.Alpha(), 0.25, AlphaTolerance);
    EXPECT_EQ(fixedStep.Accumulated(), 10ms);
}

TEST(FixedStepTest, AlphaIsTheFractionLeft)
{
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(50ms), 1);
    EXPECT_NEAR(fixedStep.Alpha(), 0.25, AlphaTolerance);
}

// 39999999 / 40000000 in float rounds to 1: the clamp keeps it below.
TEST(FixedStepTest, AlphaStaysBelowOne)
{
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(39'999'999ns), 0);
    EXPECT_LT(fixedStep.Alpha(), 1.0f);
}

TEST(FixedStepTest, ANegativeFrameCountsZero)
{
    FixedStep fixedStep;
    ASSERT_EQ(fixedStep.Advance(20ms), 0);

    EXPECT_EQ(fixedStep.Advance(-5ms), 0);
    EXPECT_EQ(fixedStep.Accumulated(), 20ms);
    EXPECT_FALSE(fixedStep.LastFrameWasClamped());
    EXPECT_EQ(fixedStep.Advance(20ms), 1);
}

TEST(FixedStepTest, TheCapIsOneSecond)
{
    static_assert(Client::Kernel::MaxFrameTime == 1s);
    FixedStep fixedStep;

    EXPECT_EQ(fixedStep.Advance(1s), 25);
    EXPECT_FALSE(fixedStep.LastFrameWasClamped());

    EXPECT_EQ(fixedStep.Advance(1s + 1ns), 25);
    EXPECT_TRUE(fixedStep.LastFrameWasClamped());

    EXPECT_EQ(fixedStep.Advance(10s), 25);
    EXPECT_TRUE(fixedStep.LastFrameWasClamped());
    EXPECT_EQ(fixedStep.Accumulated(), 0ns);
}
} // namespace
