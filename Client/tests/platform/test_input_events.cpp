#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"
#include "Input/TextInputEvent.h"
#include "Platform/InputEvents.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::Platform::KeyFor e InputEventFor: de SDL a Input, con el SDL_Event armado a mano, sin
// SDL_Init y sin ventana (plan de la CL3, sub-ola CL3.3).
namespace
{
using Client::Input::InputEvent;
using Client::Input::Key;
using Client::Input::KeyCount;
using Client::Input::MouseButton;
using Client::Platform::InputEventFor;
using Client::Platform::KeyFor;

SDL_Event KeyboardEvent(std::uint32_t type, SDL_Scancode scancode, bool repeat = false)
{
    SDL_Event event{};
    event.type = type;
    event.key.scancode = scancode;
    event.key.down = type == SDL_EVENT_KEY_DOWN;
    event.key.repeat = repeat;
    return event;
}

SDL_Event ButtonEvent(std::uint32_t type, std::uint8_t button)
{
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    return event;
}

SDL_Event WheelEvent(float y, std::int32_t integerY, SDL_MouseWheelDirection direction)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = y;
    event.wheel.integer_y = integerY;
    event.wheel.direction = direction;
    return event;
}

TEST(InputEventsTest, MapsTheScancodes)
{
    EXPECT_EQ(KeyFor(SDL_SCANCODE_A), Key::A);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_Z), Key::Z);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_RETURN), Key::Enter);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_ESCAPE), Key::Escape);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_1), Key::Digit1);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_9), Key::Digit9);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_0), Key::Digit0);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_F1), Key::F1);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_F12), Key::F12);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_BACKSPACE), Key::Backspace);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_HOME), Key::Home);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_END), Key::End);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_DELETE), Key::Delete);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_LSHIFT), Key::ShiftLeft);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_RSHIFT), Key::ShiftRight);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_LCTRL), Key::CtrlLeft);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_RCTRL), Key::CtrlRight);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_LALT), Key::AltLeft);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_RALT), Key::AltRight);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_CAPSLOCK), std::nullopt);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_KP_ENTER), std::nullopt);
    EXPECT_EQ(KeyFor(SDL_SCANCODE_F13), std::nullopt);
}

TEST(InputEventsTest, EveryPhysicalKeyHasExactlyOneScancode)
{
    std::vector<int> scancodesPerKey(KeyCount, 0);
    for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode)
    {
        if (const auto key = KeyFor(static_cast<SDL_Scancode>(scancode)))
        {
            ++scancodesPerKey[static_cast<std::size_t>(*key)];
        }
    }

    for (std::size_t index = 0; index < KeyCount; ++index)
    {
        const auto key = static_cast<Key>(index);
        // Shift, Ctrl y Alt son lógicas: las arma InputState con sus lados.
        const bool logical = key == Key::Shift || key == Key::Ctrl || key == Key::Alt;
        EXPECT_EQ(scancodesPerKey[index], logical ? 0 : 1) << "tecla " << index;
    }
}

TEST(InputEventsTest, AutoRepeatIsNotAnEvent)
{
    EXPECT_EQ(InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_A, true)), std::nullopt);
}

TEST(InputEventsTest, RepeatedNavigationAndDeleteRemainEditingEvents)
{
    for (const auto [scancode, key] : {std::pair{SDL_SCANCODE_LEFT, Key::Left}, std::pair{SDL_SCANCODE_HOME, Key::Home},
                                       std::pair{SDL_SCANCODE_DELETE, Key::Delete}})
    {
        const auto repeated = InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, scancode, true));
        ASSERT_TRUE(repeated.has_value());
        EXPECT_EQ(repeated->kind, InputEvent::Kind::KeyDown);
        EXPECT_EQ(repeated->key, key);
        EXPECT_TRUE(repeated->repeat);
    }
}

// Backspace, Tab y Enter se repiten como KeyDown para que InputState adjunte los modificadores
// actuales sin crear un flanco físico.
TEST(InputEventsTest, RepeatedControlKeysRemainEditingKeys)
{
    for (const auto [scancode, key] :
         {std::pair{SDL_SCANCODE_BACKSPACE, Key::Backspace}, std::pair{SDL_SCANCODE_TAB, Key::Tab},
          std::pair{SDL_SCANCODE_RETURN, Key::Enter}})
    {
        const auto repeated = InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, scancode, true));
        ASSERT_TRUE(repeated.has_value());
        EXPECT_EQ(repeated->kind, InputEvent::Kind::KeyDown);
        EXPECT_EQ(repeated->key, key);
        EXPECT_TRUE(repeated->repeat);
    }
}

TEST(InputEventsTest, ARepeatedControlKeyKeepsCurrentModifiersWithoutAKeyPressEdge)
{
    Client::Input::InputState input;
    input.Apply(*InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_LCTRL)));
    input.Apply(*InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_BACKSPACE, true)));

    const auto events = input.TextInputEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, Client::Input::TextInputEvent::Kind::Key);
    EXPECT_EQ(events[0].key, Key::Backspace);
    EXPECT_TRUE(events[0].ctrl);
    EXPECT_FALSE(input.WasPressed(Key::Backspace));
    EXPECT_TRUE(input.IsDown(Key::Ctrl));
    EXPECT_EQ(input.Text().size(), 1u);
    EXPECT_EQ(input.Text()[0], Client::Input::BackspaceChar);
}

TEST(InputEventsTest, KeyDownAndUpCarryTheKey)
{
    const auto down = InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE));
    ASSERT_TRUE(down.has_value());
    EXPECT_EQ(down->kind, InputEvent::Kind::KeyDown);
    EXPECT_EQ(down->key, Key::Escape);

    const auto up = InputEventFor(KeyboardEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_LSHIFT));
    ASSERT_TRUE(up.has_value());
    EXPECT_EQ(up->kind, InputEvent::Kind::KeyUp);
    EXPECT_EQ(up->key, Key::ShiftLeft);

    EXPECT_EQ(InputEventFor(KeyboardEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_CAPSLOCK)), std::nullopt);
}

TEST(InputEventsTest, MapsTheMouseButtons)
{
    const auto left = InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT));
    ASSERT_TRUE(left.has_value());
    EXPECT_EQ(left->kind, InputEvent::Kind::ButtonDown);
    EXPECT_EQ(left->button, MouseButton::Left);

    const auto middle = InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_MIDDLE));
    ASSERT_TRUE(middle.has_value());
    EXPECT_EQ(middle->kind, InputEvent::Kind::ButtonUp);
    EXPECT_EQ(middle->button, MouseButton::Middle);

    const auto right = InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT));
    ASSERT_TRUE(right.has_value());
    EXPECT_EQ(right->button, MouseButton::Right);

    EXPECT_EQ(InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_X1)), std::nullopt);
    EXPECT_EQ(InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_X2)), std::nullopt);
}

// 0x4b349d -> 0x4b4444: el segundo DOWN de SDL representa WM_LBUTTONDBLCLK y no otro WM_LBUTTONDOWN.
TEST(InputEventsTest, EvenLeftPressesCarryADifferentEventFromOrdinaryPresses)
{
    SDL_Event ordinary = ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    ordinary.button.clicks = 1;
    const auto first = InputEventFor(ordinary);
    ASSERT_TRUE(first.has_value());
    for (const std::uint8_t clicks : {std::uint8_t{2}, std::uint8_t{4}})
    {
        SDL_Event doubled = ordinary;
        doubled.button.clicks = clicks;
        const auto second = InputEventFor(doubled);
        ASSERT_TRUE(second.has_value());
        EXPECT_NE(second->kind, first->kind) << static_cast<int>(clicks);
        EXPECT_EQ(second->button, MouseButton::Left);
    }
    ordinary.button.clicks = 3;
    ASSERT_TRUE(InputEventFor(ordinary).has_value());
    EXPECT_EQ(InputEventFor(ordinary)->kind, first->kind);
}

// La selección sigue viendo el segundo apretón físico; los flags de WM_LBUTTONDOWN del juego no lo ven.
TEST(InputEventsTest, ADoubleClickKeepsThePhysicalPressWithoutAnotherGamePress)
{
    Client::Input::InputState input;
    SDL_Event down = ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    down.button.clicks = 1;
    input.Apply(*InputEventFor(down));
    EXPECT_TRUE(input.MouseMessages().leftPress);
    EXPECT_TRUE(input.MouseMessages().leftDown);
    input.ConsumeEdges();
    input.Apply(*InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT)));
    input.ConsumeEdges();
    down.button.clicks = 2;

    input.Apply(*InputEventFor(down));

    EXPECT_TRUE(input.IsDown(MouseButton::Left));
    EXPECT_TRUE(input.WasPressed(MouseButton::Left));
    EXPECT_TRUE(input.MouseMessages().doubleClick);
    EXPECT_FALSE(input.MouseMessages().leftPress);
    EXPECT_FALSE(input.MouseMessages().leftDown);
    input.ConsumeEdges();
    // 0x4b3444 lo borra al llegar otro mensaje, no al consumir los flancos del paso.
    EXPECT_TRUE(input.MouseMessages().doubleClick);
    EXPECT_FALSE(input.MouseMessages().leftDown);
}

TEST(InputEventsTest, ADownUpDoubleDownBatchHasNoOrdinaryGamePress)
{
    Client::Input::InputState input;
    SDL_Event down = ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    down.button.clicks = 1;
    input.Apply(*InputEventFor(down));
    input.Apply(*InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT)));
    down.button.clicks = 2;

    input.Apply(*InputEventFor(down));

    EXPECT_TRUE(input.MouseMessages().doubleClick);
    EXPECT_FALSE(input.MouseMessages().leftPress);
    EXPECT_FALSE(input.MouseMessages().leftDown);
    EXPECT_TRUE(input.WasPressed(MouseButton::Left));
}

TEST(InputEventsTest, TheNextMessageClearsTheDoubleClickAndTheThirdPressIsOrdinary)
{
    Client::Input::InputState input;
    SDL_Event down = ButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    down.button.clicks = 2;
    input.Apply(*InputEventFor(down));
    ASSERT_TRUE(input.MouseMessages().doubleClick);

    input.Apply(InputEvent{.kind = InputEvent::Kind::Message});
    EXPECT_FALSE(input.MouseMessages().doubleClick);
    input.Apply(*InputEventFor(ButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT)));
    down.button.clicks = 3;
    input.Apply(*InputEventFor(down));
    EXPECT_FALSE(input.MouseMessages().doubleClick);
    EXPECT_TRUE(input.MouseMessages().leftPress);
    EXPECT_TRUE(input.MouseMessages().leftDown);
}

TEST(InputEventsTest, TheWheelIsInWholeNotches)
{
    const auto forward = InputEventFor(WheelEvent(2.0f, 2, SDL_MOUSEWHEEL_NORMAL));
    ASSERT_TRUE(forward.has_value());
    EXPECT_EQ(forward->kind, InputEvent::Kind::Wheel);
    EXPECT_FLOAT_EQ(forward->wheel, 2.0f);

    const auto back = InputEventFor(WheelEvent(-1.0f, -1, SDL_MOUSEWHEEL_NORMAL));
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->wheel, -1.0f);

    // Un touchpad que todavía no juntó una muesca entera.
    EXPECT_EQ(InputEventFor(WheelEvent(0.4f, 0, SDL_MOUSEWHEEL_NORMAL)), std::nullopt);

    // Y el que la completa con lo que le sobró del evento anterior: truncar `y` daría 0, y
    // redondear 0,3 también.
    for (const float y : {0.6f, 0.3f})
    {
        const auto completed = InputEventFor(WheelEvent(y, 1, SDL_MOUSEWHEEL_NORMAL));
        ASSERT_TRUE(completed.has_value()) << "y " << y;
        EXPECT_FLOAT_EQ(completed->wheel, 1.0f) << "y " << y;
    }
}

TEST(InputEventsTest, TheLowestWheelIsNotAnEvent)
{
    // Negarlo sería UB con FLIPPED; no es un giro real.
    const std::int32_t lowest = std::numeric_limits<std::int32_t>::min();
    EXPECT_EQ(InputEventFor(WheelEvent(0.0f, lowest, SDL_MOUSEWHEEL_FLIPPED)), std::nullopt);
    EXPECT_EQ(InputEventFor(WheelEvent(0.0f, lowest, SDL_MOUSEWHEEL_NORMAL)), std::nullopt);
}

TEST(InputEventsTest, AFlippedWheelIsTurnedBack)
{
    // Con el scroll "natural", SDL da los valores al revés: adelante es negativo.
    const auto forward = InputEventFor(WheelEvent(-1.0f, -1, SDL_MOUSEWHEEL_FLIPPED));
    ASSERT_TRUE(forward.has_value());
    EXPECT_FLOAT_EQ(forward->wheel, 1.0f);

    // Con el resto del evento anterior, también al revés.
    for (const float y : {-0.6f, -0.3f})
    {
        const auto completed = InputEventFor(WheelEvent(y, -1, SDL_MOUSEWHEEL_FLIPPED));
        ASSERT_TRUE(completed.has_value()) << "y " << y;
        EXPECT_FLOAT_EQ(completed->wheel, 1.0f) << "y " << y;
    }
}

TEST(InputEventsTest, MotionCarriesThePosition)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 12.5f;
    event.motion.y = 40.0f;

    const auto motion = InputEventFor(event);
    ASSERT_TRUE(motion.has_value());
    EXPECT_EQ(motion->kind, InputEvent::Kind::Motion);
    EXPECT_FLOAT_EQ(motion->x, 12.5f);
    EXPECT_FLOAT_EQ(motion->y, 40.0f);
}

// Sin la densidad de la ventana, la posición es la del evento.
TEST(InputEventsTest, TheMouseIsInFramebufferPixels)
{
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 12.5f;
    event.motion.y = 40.0f;

    // Con densidad 2 (una ventana de 640 × 480 con un framebuffer de 1280 × 960), el doble.
    const auto dense = InputEventFor(event, 2.0f);
    ASSERT_TRUE(dense.has_value());
    EXPECT_FLOAT_EQ(dense->x, 25.0f);
    EXPECT_FLOAT_EQ(dense->y, 80.0f);

    // SDL_GetWindowPixelDensity da 0 si falla: la posición queda como llegó.
    const auto failed = InputEventFor(event, 0.0f);
    ASSERT_TRUE(failed.has_value());
    EXPECT_FLOAT_EQ(failed->x, 12.5f);
    EXPECT_FLOAT_EQ(failed->y, 40.0f);
}

// El texto del SDL_EVENT_TEXT_INPUT, copiado: SDL lo libera en el próximo SDL_PollEvent.
TEST(InputEventsTest, TranslatesTheTextInput)
{
    std::string typed = "a\xc3\xa1";
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = typed.c_str();

    const auto text = InputEventFor(event);
    typed.assign("zz");

    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(text->kind, InputEvent::Kind::Text);
    EXPECT_EQ(text->text, "a\xc3\xa1");
}

// Uno sin texto no es una entrada; la composición IME es un evento aparte, incluso al cancelarse.
TEST(InputEventsTest, EmptyTextIsIgnoredAndImeCompositionIsCopied)
{
    SDL_Event empty{};
    empty.type = SDL_EVENT_TEXT_INPUT;
    empty.text.text = "";
    EXPECT_EQ(InputEventFor(empty), std::nullopt);

    SDL_Event missing{};
    missing.type = SDL_EVENT_TEXT_INPUT;
    missing.text.text = nullptr;
    EXPECT_EQ(InputEventFor(missing), std::nullopt);

    SDL_Event editing{};
    editing.type = SDL_EVENT_TEXT_EDITING;
    std::string composition = "か";
    editing.edit.text = composition.c_str();
    editing.edit.start = 1;
    editing.edit.length = 2;
    const auto composed = InputEventFor(editing);
    composition = "x";
    ASSERT_TRUE(composed.has_value());
    EXPECT_EQ(composed->kind, InputEvent::Kind::Composition);
    EXPECT_EQ(composed->text, "か");
    EXPECT_EQ(composed->compositionStart, 1);
    EXPECT_EQ(composed->compositionLength, 2);

    editing.edit.text = "";
    const auto cancelled = InputEventFor(editing);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->kind, InputEvent::Kind::Composition);
    EXPECT_TRUE(cancelled->text.empty());
}

TEST(InputEventsTest, FocusLostIsAnEvent)
{
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;

    const auto focusLost = InputEventFor(event);
    ASSERT_TRUE(focusLost.has_value());
    EXPECT_EQ(focusLost->kind, InputEvent::Kind::FocusLost);
}

TEST(InputEventsTest, OtherEventsAreNotInput)
{
    for (const std::uint32_t type : {std::uint32_t{SDL_EVENT_QUIT}, std::uint32_t{SDL_EVENT_WINDOW_FOCUS_GAINED},
                                     std::uint32_t{SDL_EVENT_WINDOW_RESIZED}})
    {
        SDL_Event event{};
        event.type = type;
        EXPECT_EQ(InputEventFor(event), std::nullopt) << "tipo " << type;
    }
}
} // namespace
