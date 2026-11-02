#include "FixedStep.h"

#include <gtest/gtest.h>

#include <chrono>
#include <limits>

// Labs::FixedStep y Labs::ChronoFixedStep: los casos del plan (CL3.0, paso 2), el resto que pasa al
// frame siguiente, y lo que se pierde al truncar cada frame a ms (paso 6).
namespace
{
using Labs::ChronoFixedStep;
using Labs::FixedStep;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;

// Del C++ de la guía, comprobado al compilar: duration_cast trunca hacia cero (también los
// negativos), y steady_clock no va para atrás. De system_clock el estándar no promete nada.
static_assert(std::chrono::duration_cast<milliseconds>(nanoseconds{1'999'999}) == milliseconds{1});
static_assert(std::chrono::duration_cast<milliseconds>(nanoseconds{-1'999'999}) == milliseconds{-1});
static_assert(std::chrono::steady_clock::is_steady);

// Lo que dura el frame i de n en un segundo, en ns enteros: (i + 1) s / n - i s / n. Los n frames
// suman 1 s exacto aunque 1 s / n no dé ns enteros (60 FPS: 16.666.666 o 16.666.667).
nanoseconds FrameOf(int i, int framesPerSecond)
{
    constexpr nanoseconds OneSecond = std::chrono::seconds{1};
    return (OneSecond * (i + 1)) / framesPerSecond - (OneSecond * i) / framesPerSecond;
}

TEST(FixedStepTest, FortyIsOneStep)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(40), 1);
    EXPECT_EQ(step.RestMs(), 0);
}

TEST(FixedStepTest, ThirtyNineIsNone)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(39), 0);
    EXPECT_EQ(step.RestMs(), 39);
}

// El "alcanzar" del 02: un frame lento corre varios pasos seguidos.
TEST(FixedStepTest, OneHundredThirtyIsThree)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(130), 3);
    EXPECT_EQ(step.RestMs(), 10);
}

// Lo que sobra no se pierde: 39 + 1 es un paso, y 10 + 30, otro.
TEST(FixedStepTest, TheRestCarriesOver)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(39), 0);
    EXPECT_EQ(step.Advance(1), 1);
    EXPECT_EQ(step.RestMs(), 0);
    EXPECT_EQ(step.Advance(130), 3);
    EXPECT_EQ(step.Advance(30), 1);
    EXPECT_EQ(step.RestMs(), 0);
}

// Un segundo son 25 pasos, a 25 FPS o a 1.
TEST(FixedStepTest, TwentyFiveFramesOfFortyAreTwentyFiveSteps)
{
    FixedStep step;
    int steps = 0;
    for (int frame = 0; frame < 25; ++frame)
    {
        steps += step.Advance(40);
    }
    EXPECT_EQ(steps, 25);
    EXPECT_EQ(step.RestMs(), 0);
}

TEST(FixedStepTest, OneFrameOfOneThousandIsTwentyFiveSteps)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(1'000), 25);
    EXPECT_EQ(step.RestMs(), 0);
}

// Un reloj que vuelve para atrás no deja el acumulador debiendo: el resto queda como estaba.
TEST(FixedStepTest, ANegativeFrameCountsZero)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(30), 0);
    EXPECT_EQ(step.Advance(-100), 0);
    EXPECT_EQ(step.RestMs(), 30);
    EXPECT_EQ(step.Advance(10), 1);
}

// Los bordes del recorte, con el resto en 0: -1 no deja el resto en -1, y 0 no corre nada.
TEST(FixedStepTest, MinusOneAndZeroFromRestZero)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(-1), 0);
    EXPECT_EQ(step.RestMs(), 0);
    EXPECT_EQ(step.Advance(0), 0);
    EXPECT_EQ(step.RestMs(), 0);
    EXPECT_EQ(step.Advance(40), 1);
    EXPECT_EQ(step.RestMs(), 0);
}

// 39 + INT_MAX no entra en un int: con la suma en 64 bits da 53.687.092 pasos y resto 6.
TEST(FixedStepTest, TheLargestFrameDoesNotOverflow)
{
    FixedStep step;
    EXPECT_EQ(step.Advance(39), 0);
    EXPECT_EQ(step.Advance(std::numeric_limits<int>::max()), 53'687'092);
    EXPECT_EQ(step.RestMs(), 6);
}

TEST(ChronoFixedStepTest, KeepsTheNanoseconds)
{
    ChronoFixedStep step;
    EXPECT_EQ(step.Advance(nanoseconds{39'999'999}), 0);
    EXPECT_EQ(step.Advance(nanoseconds{1}), 1);
    EXPECT_EQ(step.Rest(), nanoseconds{0});
    EXPECT_EQ(step.Advance(milliseconds{130}), 3);
    EXPECT_EQ(step.Rest(), milliseconds{10});
    EXPECT_EQ(step.Advance(nanoseconds{-5}), 0);
    EXPECT_EQ(step.Rest(), milliseconds{10});
}

// Una hora son 90.000 pasos, y en nanosegundos no sobra nada.
TEST(ChronoFixedStepTest, OneHourIsNinetyThousandSteps)
{
    ChronoFixedStep step;
    EXPECT_EQ(step.Advance(std::chrono::hours{1}), 90'000);
    EXPECT_EQ(step.Rest(), nanoseconds{0});
}

// El frame más largo que entra en un nanoseconds, con 1 ns de resto: sin el recorte, la suma se
// pasa de los 64 bits (UBSan lo marca) y los pasos, de un int. Recortado, cuenta INT_MAX pasos y
// el resto sigue en 1 ns. El más negativo cuenta 0.
TEST(ChronoFixedStepTest, TheLargestFrameCountsIntMaxSteps)
{
    ChronoFixedStep step;
    EXPECT_EQ(step.Advance(nanoseconds{1}), 0);
    EXPECT_EQ(step.Advance(nanoseconds::max()), std::numeric_limits<int>::max());
    EXPECT_EQ(step.Rest(), nanoseconds{1});
    EXPECT_EQ(step.Advance(nanoseconds::min()), 0);
    EXPECT_EQ(step.Rest(), nanoseconds{1});
}

// El paso 6: 60 frames que suman 1 s. Truncado a ms, cada uno cuenta 16 y el segundo cuenta 960 ms:
// 24 pasos. En nanosegundos, los 25.
TEST(ChronoFixedStepTest, TruncatingEachFrameToMillisecondsLosesAStep)
{
    constexpr int FramesPerSecond = 60;
    FixedStep inMs;
    ChronoFixedStep inNs;
    int stepsInMs = 0;
    int stepsInNs = 0;
    for (int i = 0; i < FramesPerSecond; ++i)
    {
        const nanoseconds frame = FrameOf(i, FramesPerSecond);
        stepsInMs += inMs.Advance(static_cast<int>(std::chrono::duration_cast<milliseconds>(frame).count()));
        stepsInNs += inNs.Advance(frame);
    }
    EXPECT_EQ(stepsInMs, 24);
    EXPECT_EQ(inMs.RestMs(), 0);
    EXPECT_EQ(stepsInNs, 25);
    EXPECT_EQ(inNs.Rest(), nanoseconds{0});
}
} // namespace
