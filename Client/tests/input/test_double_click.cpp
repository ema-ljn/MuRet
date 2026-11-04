#include "Input/DoubleClick.h"

#include "Core/IClock.h"
#include "Input/InputState.h"

#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <chrono>

// Client::Input::DoubleClick: el reloj propio de 0x640940 (Q-C99).
namespace
{
using Client::Core::Duration;
using Client::Input::DoubleClick;
using Client::Input::InputState;
using Client::Tests::MoveMouse;
using Client::Tests::PressLeft;
using Client::Tests::ReleaseLeft;
using std::chrono::milliseconds;

// Un paso con un apretón y su suelta en (x, y), y los flancos consumidos después.
bool ClickStep(DoubleClick& detector, InputState& input, int x, int y, Duration now)
{
    MoveMouse(input, x, y);
    PressLeft(input);
    ReleaseLeft(input);
    const bool doubled = detector.Step(input, now);
    input.ConsumeEdges();
    return doubled;
}

TEST(SelectInputTest, TheSecondPressInTimeIsADoubleClick)
{
    DoubleClick detector;
    InputState input;

    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1000}));
    EXPECT_TRUE(ClickStep(detector, input, 100, 100, milliseconds{1400}));
    // El tercero empieza otro.
    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1500}));
}

TEST(SelectInputTest, ALateSecondPressIsAFirstAgain)
{
    DoubleClick detector;
    InputState input;

    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1000}));
    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1501}));
    EXPECT_TRUE(ClickStep(detector, input, 100, 100, milliseconds{1900}));
}

// 0x640940 arma con `límite < ahora - primero`: a los 500 ms justos todavía es doble.
TEST(SelectInputTest, ThePressAtExactlyTheLimitIsStillADoubleClick)
{
    DoubleClick detector;
    InputState input;

    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1000}));
    EXPECT_TRUE(ClickStep(detector, input, 100, 100, milliseconds{1000} + Client::Input::DoubleClickTime));
}

// Q-C99: un píxel de movimiento entre los dos clics lo cancela.
TEST(SelectInputTest, MovingBetweenClicksIsNotADoubleClick)
{
    DoubleClick detector;
    InputState input;

    EXPECT_FALSE(ClickStep(detector, input, 100, 100, milliseconds{1000}));
    MoveMouse(input, 101, 100);
    detector.Step(input, milliseconds{1100});
    input.ConsumeEdges();
    EXPECT_FALSE(ClickStep(detector, input, 101, 100, milliseconds{1200}));
}
} // namespace
