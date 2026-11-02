#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"

#include <gtest/gtest.h>

// Client::Input::InputState: el teclado y el mouse por frame, con sus flancos (plan de la CL3,
// sub-ola CL3.3, la tabla del paso 1).
namespace
{
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Input::MouseButton;

InputEvent KeyEvent(InputEvent::Kind kind, Key key)
{
    InputEvent event{.kind = kind};
    event.key = key;
    return event;
}

InputEvent ButtonEvent(InputEvent::Kind kind, MouseButton button)
{
    InputEvent event{.kind = kind};
    event.button = button;
    return event;
}

InputEvent WheelEvent(float notches)
{
    InputEvent event{.kind = InputEvent::Kind::Wheel};
    event.wheel = notches;
    return event;
}

TEST(InputStateTest, AHeldKeyIsPressedOnce)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::A));

    // Frame 1: abajo y recién apretada.
    EXPECT_TRUE(input.IsDown(Key::A));
    EXPECT_TRUE(input.WasPressed(Key::A));
    input.ConsumeEdges();

    // Frames 2 y 3: sigue abajo, pero el apretón ya se consumió.
    for (int frame = 2; frame <= 3; ++frame)
    {
        EXPECT_TRUE(input.IsDown(Key::A)) << "frame " << frame;
        EXPECT_FALSE(input.WasPressed(Key::A)) << "frame " << frame;
        EXPECT_FALSE(input.WasReleased(Key::A)) << "frame " << frame;
        input.ConsumeEdges();
    }
}

TEST(InputStateTest, APressAndReleaseInOneFrameIsSeen)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Enter));
    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::Enter));

    EXPECT_TRUE(input.WasPressed(Key::Enter));
    EXPECT_TRUE(input.WasReleased(Key::Enter));
    EXPECT_FALSE(input.IsDown(Key::Enter));

    input.ConsumeEdges();
    EXPECT_FALSE(input.WasPressed(Key::Enter));
    EXPECT_FALSE(input.WasReleased(Key::Enter));
}

TEST(InputStateTest, ASecondKeyDownIsNotAnEdge)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Escape));
    input.ConsumeEdges();

    // El auto-repeat lo filtra Platform; un KeyDown más con la tecla abajo tampoco es un apretón.
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Escape));

    EXPECT_TRUE(input.IsDown(Key::Escape));
    EXPECT_FALSE(input.WasPressed(Key::Escape));
}

TEST(InputStateTest, FocusLostReleasesEverything)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Shift));
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Left));
    input.ConsumeEdges();

    input.Apply(InputEvent{.kind = InputEvent::Kind::FocusLost});

    EXPECT_FALSE(input.IsDown(Key::Shift));
    EXPECT_TRUE(input.WasReleased(Key::Shift));
    EXPECT_FALSE(input.IsDown(MouseButton::Left));
    EXPECT_TRUE(input.WasReleased(MouseButton::Left));
}

TEST(InputStateTest, TheWheelAddsUntilConsumed)
{
    InputState input;
    input.Apply(WheelEvent(1.0f));
    input.Apply(WheelEvent(2.0f));

    EXPECT_FLOAT_EQ(input.Wheel(), 3.0f);

    input.ConsumeEdges();
    EXPECT_FLOAT_EQ(input.Wheel(), 0.0f);
}

TEST(InputStateTest, MouseButtonsAreEdgesToo)
{
    InputState input;
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Right));

    EXPECT_TRUE(input.IsDown(MouseButton::Right));
    EXPECT_TRUE(input.WasPressed(MouseButton::Right));
    EXPECT_FALSE(input.WasReleased(MouseButton::Right));
    input.ConsumeEdges();

    input.Apply(ButtonEvent(InputEvent::Kind::ButtonUp, MouseButton::Right));

    EXPECT_FALSE(input.IsDown(MouseButton::Right));
    EXPECT_FALSE(input.WasPressed(MouseButton::Right));
    EXPECT_TRUE(input.WasReleased(MouseButton::Right));
}
} // namespace
