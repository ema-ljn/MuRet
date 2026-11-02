#include "App/FixedStep.h"

#include "App/LoopTiming.h"

#include <gtest/gtest.h>

#include <chrono>

// Client::App::FixedStep: el paso fijo de 40 ms del modo normal, con su alfa y su tope.
namespace
{
using namespace std::chrono_literals;
using Client::App::FixedStep;

// La tolerancia de los alfas que fija el plan (1e-6).
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

// El alcanzar del 02: un frame de 130 ms corre 3 pasos y deja 10 ms.
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

// 39.999.999 / 40.000.000 en float redondea a 1: el recorte lo deja debajo.
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
    static_assert(Client::App::MaxFrameTime == 1s);
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
