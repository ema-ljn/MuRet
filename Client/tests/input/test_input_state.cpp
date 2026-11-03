#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <string>
#include <utility>

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

InputEvent TextEvent(std::string text)
{
    InputEvent event{.kind = InputEvent::Kind::Text};
    event.text = std::move(text);
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
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::ShiftLeft));
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Left));
    input.ConsumeEdges();

    input.Apply(InputEvent{.kind = InputEvent::Kind::FocusLost});

    EXPECT_FALSE(input.IsDown(Key::ShiftLeft));
    EXPECT_TRUE(input.WasReleased(Key::ShiftLeft));
    EXPECT_FALSE(input.IsDown(Key::Shift));
    EXPECT_TRUE(input.WasReleased(Key::Shift));
    EXPECT_FALSE(input.IsDown(MouseButton::Left));
    EXPECT_TRUE(input.WasReleased(MouseButton::Left));
}

TEST(InputStateTest, FocusLostClearsTheWheelAndThePresses)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::A));
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Left));
    input.Apply(WheelEvent(2.0f));

    input.Apply(InputEvent{.kind = InputEvent::Kind::FocusLost});

    // Apretadas en el mismo frame: no / no / sí. El paso siguiente no ve el clic ni la rueda.
    EXPECT_FALSE(input.IsDown(Key::A));
    EXPECT_FALSE(input.WasPressed(Key::A));
    EXPECT_TRUE(input.WasReleased(Key::A));
    EXPECT_FALSE(input.IsDown(MouseButton::Left));
    EXPECT_FALSE(input.WasPressed(MouseButton::Left));
    EXPECT_TRUE(input.WasReleased(MouseButton::Left));
    EXPECT_FLOAT_EQ(input.Wheel(), 0.0f);
}

TEST(InputStateTest, FocusLostDoesNotReleaseWhatWasUp)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::A));
    input.ConsumeEdges();

    input.Apply(InputEvent{.kind = InputEvent::Kind::FocusLost});

    EXPECT_TRUE(input.WasReleased(Key::A));
    EXPECT_FALSE(input.WasReleased(Key::B));
    EXPECT_FALSE(input.WasReleased(MouseButton::Right));
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

TEST(InputStateTest, TheWheelGoesBothWays)
{
    InputState input;
    input.Apply(WheelEvent(2.0f));
    input.Apply(WheelEvent(-3.0f));

    EXPECT_FLOAT_EQ(input.Wheel(), -1.0f);
}

TEST(InputStateTest, ANonFiniteWheelIsDiscarded)
{
    InputState input;
    input.Apply(WheelEvent(std::numeric_limits<float>::quiet_NaN()));
    input.Apply(WheelEvent(std::numeric_limits<float>::infinity()));
    input.Apply(WheelEvent(-std::numeric_limits<float>::infinity()));
    input.Apply(WheelEvent(1.0f));

    EXPECT_FLOAT_EQ(input.Wheel(), 1.0f);
}

TEST(InputStateTest, MotionKeepsTheLastPosition)
{
    InputState input;
    EXPECT_FLOAT_EQ(input.MouseX(), 0.0f);
    EXPECT_FLOAT_EQ(input.MouseY(), 0.0f);

    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = 10.0f, .y = 20.0f});
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = 300.0f, .y = 140.0f});

    EXPECT_FLOAT_EQ(input.MouseX(), 300.0f);
    EXPECT_FLOAT_EQ(input.MouseY(), 140.0f);
    // La posición no es un flanco: ConsumeEdges no la toca.
    input.ConsumeEdges();
    EXPECT_FLOAT_EQ(input.MouseX(), 300.0f);
    EXPECT_FLOAT_EQ(input.MouseY(), 140.0f);
}

TEST(InputStateTest, MouseButtonsAreEdgesToo)
{
    InputState input;
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Right));

    EXPECT_TRUE(input.IsDown(MouseButton::Right));
    EXPECT_TRUE(input.WasPressed(MouseButton::Right));
    EXPECT_FALSE(input.WasReleased(MouseButton::Right));
    input.ConsumeEdges();

    // Un segundo ButtonDown con el botón abajo no es un apretón.
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonDown, MouseButton::Right));
    EXPECT_TRUE(input.IsDown(MouseButton::Right));
    EXPECT_FALSE(input.WasPressed(MouseButton::Right));

    input.Apply(ButtonEvent(InputEvent::Kind::ButtonUp, MouseButton::Right));

    EXPECT_FALSE(input.IsDown(MouseButton::Right));
    EXPECT_FALSE(input.WasPressed(MouseButton::Right));
    EXPECT_TRUE(input.WasReleased(MouseButton::Right));

    input.ConsumeEdges();
    EXPECT_FALSE(input.WasReleased(MouseButton::Right));
}

// Q-C47: el original acota la posición de la UI nueva al tamaño de la ventana (0x640940); acá
// queda la del evento, también fuera de la ventana (con el mouse capturado), hasta la CL5.
TEST(InputStateTest, MotionIsNotClamped)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = -5.0f, .y = 700.0f});

    EXPECT_FLOAT_EQ(input.MouseX(), -5.0f);
    EXPECT_FLOAT_EQ(input.MouseY(), 700.0f);
}

TEST(InputStateTest, APressReleaseAndPressInOneFrameKeepsBothEdges)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Space));
    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::Space));
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Space));

    // sí / sí / sí, y después de ConsumeEdges sí / no / no.
    EXPECT_TRUE(input.IsDown(Key::Space));
    EXPECT_TRUE(input.WasPressed(Key::Space));
    EXPECT_TRUE(input.WasReleased(Key::Space));

    input.ConsumeEdges();
    EXPECT_TRUE(input.IsDown(Key::Space));
    EXPECT_FALSE(input.WasPressed(Key::Space));
    EXPECT_FALSE(input.WasReleased(Key::Space));
}

TEST(InputStateTest, AKeyUpWithTheKeyUpIsNotAnEdge)
{
    // Una tecla o un botón apretados antes de tener el foco: llega la soltada sin el apretón.
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::Tab));
    input.Apply(ButtonEvent(InputEvent::Kind::ButtonUp, MouseButton::Middle));

    EXPECT_FALSE(input.IsDown(Key::Tab));
    EXPECT_FALSE(input.WasReleased(Key::Tab));
    EXPECT_FALSE(input.IsDown(MouseButton::Middle));
    EXPECT_FALSE(input.WasReleased(MouseButton::Middle));
}

TEST(InputStateTest, CountIsNeverDown)
{
    InputState input;
    for (const Key notAKey : {Key::Count, static_cast<Key>(0xFFFF)})
    {
        input.Apply(KeyEvent(InputEvent::Kind::KeyDown, notAKey));

        EXPECT_FALSE(input.IsDown(notAKey));
        EXPECT_FALSE(input.WasPressed(notAKey));
    }
}

TEST(InputStateTest, ADefaultEventDoesNothing)
{
    InputState input;
    input.Apply(InputEvent{});

    for (std::size_t index = 0; index < Client::Input::KeyCount; ++index)
    {
        EXPECT_FALSE(input.WasPressed(static_cast<Key>(index))) << "tecla " << index;
    }
}

TEST(InputStateTest, EitherSideHoldsTheModifier)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::CtrlRight));

    EXPECT_TRUE(input.IsDown(Key::Ctrl));
    EXPECT_TRUE(input.WasPressed(Key::Ctrl));
    EXPECT_TRUE(input.IsDown(Key::CtrlRight));
    EXPECT_FALSE(input.IsDown(Key::CtrlLeft));
    EXPECT_FALSE(input.IsDown(Key::Shift));
    EXPECT_FALSE(input.IsDown(Key::Alt));
    input.ConsumeEdges();

    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::CtrlRight));
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::AltLeft));

    EXPECT_FALSE(input.IsDown(Key::Ctrl));
    EXPECT_TRUE(input.WasReleased(Key::Ctrl));
    EXPECT_TRUE(input.IsDown(Key::Alt));
    EXPECT_TRUE(input.WasPressed(Key::Alt));
}

TEST(InputStateTest, ReleasingOneSideKeepsTheModifierDown)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::ShiftLeft));
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::ShiftRight));
    input.ConsumeEdges();

    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::ShiftLeft));

    EXPECT_TRUE(input.IsDown(Key::Shift));
    EXPECT_FALSE(input.WasReleased(Key::Shift));
    EXPECT_TRUE(input.WasReleased(Key::ShiftLeft));
    input.ConsumeEdges();

    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::ShiftRight));

    EXPECT_FALSE(input.IsDown(Key::Shift));
    EXPECT_TRUE(input.WasReleased(Key::Shift));
}

TEST(InputStateTest, TheModifierEdgesAreThoseOfTheOr)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::AltLeft));
    input.ConsumeEdges();

    // El segundo lado no es un apretón de Alt, aunque sí del lado.
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::AltRight));
    EXPECT_FALSE(input.WasPressed(Key::Alt));
    EXPECT_TRUE(input.WasPressed(Key::AltRight));

    // Soltar uno y volver a apretarlo, con el otro abajo, no da flancos de Alt.
    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::AltLeft));
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::AltLeft));
    EXPECT_TRUE(input.IsDown(Key::Alt));
    EXPECT_FALSE(input.WasPressed(Key::Alt));
    EXPECT_FALSE(input.WasReleased(Key::Alt));
}

TEST(InputStateTest, ALogicalKeyEventIsIgnored)
{
    InputState input;
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::Shift));

    EXPECT_FALSE(input.IsDown(Key::Shift));
    EXPECT_FALSE(input.WasPressed(Key::Shift));

    // Con un lado abajo, un KeyUp lógico no lo suelta.
    input.Apply(KeyEvent(InputEvent::Kind::KeyDown, Key::ShiftLeft));
    input.ConsumeEdges();
    input.Apply(KeyEvent(InputEvent::Kind::KeyUp, Key::Shift));

    EXPECT_TRUE(input.IsDown(Key::Shift));
    EXPECT_FALSE(input.WasReleased(Key::Shift));
}

// El texto tecleado (CL5.5): lo de cada Text se junta, en orden, hasta ConsumeEdges, como la rueda.
TEST(InputStateTest, CollectsTheTypedText)
{
    InputState input;
    EXPECT_EQ(input.Text(), "");

    input.Apply(TextEvent("ab"));
    input.Apply(TextEvent("c"));

    EXPECT_EQ(input.Text(), "abc");
}

TEST(InputStateTest, ConsumeEdgesEmptiesIt)
{
    InputState input;
    input.Apply(TextEvent("ab"));

    input.ConsumeEdges();
    EXPECT_EQ(input.Text(), "");

    input.Apply(TextEvent("d"));
    EXPECT_EQ(input.Text(), "d");
}

// Como un clic de antes del alt-tab, lo tecleado antes de perder el foco no lo ve el paso.
TEST(InputStateTest, FocusLostEmptiesTheText)
{
    InputState input;
    input.Apply(TextEvent("ab"));

    input.Apply(InputEvent{.kind = InputEvent::Kind::FocusLost});

    EXPECT_EQ(input.Text(), "");
}

// El texto no es una tecla: no aprieta ninguna.
TEST(InputStateTest, TheTextPressesNoKey)
{
    InputState input;
    input.Apply(TextEvent("a"));

    EXPECT_FALSE(input.WasPressed(Key::A));
    EXPECT_FALSE(input.IsDown(Key::A));
}
} // namespace
