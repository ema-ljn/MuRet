#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Platform/InputEvents.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
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
