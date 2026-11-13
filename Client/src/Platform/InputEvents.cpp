#include "Platform/InputEvents.h"

#include "Input/InputEvent.h"
#include "Input/Key.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace Client::Platform
{
namespace
{
using Input::InputEvent;
using Input::Key;
using Input::MouseButton;

// La tecla que está `offset` lugares después de `first` en el enum, para los grupos que SDL y Key
// tienen en el mismo orden (las letras, los dígitos y las F).
Key KeyAfter(Key first, int offset)
{
    return static_cast<Key>(static_cast<int>(first) + offset);
}

// Lo que KeyAfter supone: cada grupo, en Key.h, tiene el largo y el orden de SDL_scancode.h.
static_assert(static_cast<int>(Key::Z) - static_cast<int>(Key::A) == SDL_SCANCODE_Z - SDL_SCANCODE_A);
static_assert(static_cast<int>(Key::Digit9) - static_cast<int>(Key::Digit1) == SDL_SCANCODE_9 - SDL_SCANCODE_1);
static_assert(static_cast<int>(Key::F12) - static_cast<int>(Key::F1) == SDL_SCANCODE_F12 - SDL_SCANCODE_F1);

std::optional<MouseButton> ButtonFor(std::uint8_t button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    default:
        return std::nullopt;
    }
}

// La repetición de Backspace, Tab y Enter, como el WM_CHAR que Windows repite al EDIT del campo: su byte
// en el texto tecleado (InputEvent.h). La de las otras teclas no es un evento.
std::optional<InputEvent> RepeatEventFor(SDL_Scancode scancode)
{
    char typed = '\0';
    switch (scancode)
    {
    case SDL_SCANCODE_BACKSPACE:
        typed = Input::BackspaceChar;
        break;
    case SDL_SCANCODE_TAB:
        typed = Input::TabChar;
        break;
    case SDL_SCANCODE_RETURN:
        typed = Input::EnterChar;
        break;
    default:
        return std::nullopt;
    }
    InputEvent event{.kind = InputEvent::Kind::Text};
    event.text = std::string(1, typed);
    return event;
}

std::optional<InputEvent> KeyEventFor(const SDL_KeyboardEvent& key, InputEvent::Kind kind)
{
    if (key.repeat)
    {
        return kind == InputEvent::Kind::KeyDown ? RepeatEventFor(key.scancode) : std::nullopt;
    }
    const auto mapped = KeyFor(key.scancode);
    if (!mapped)
    {
        return std::nullopt;
    }
    return InputEvent{.kind = kind, .key = *mapped};
}

std::optional<InputEvent> ButtonEventFor(const SDL_MouseButtonEvent& button, InputEvent::Kind kind)
{
    const auto mapped = ButtonFor(button.button);
    if (!mapped)
    {
        return std::nullopt;
    }
    return InputEvent{.kind = kind, .button = *mapped, .clicks = button.clicks};
}

std::optional<InputEvent> MotionEventFor(const SDL_MouseMotionEvent& motion, float pixelDensity)
{
    const float density = pixelDensity > 0.0f && std::isfinite(pixelDensity) ? pixelDensity : 1.0f;
    return InputEvent{.kind = InputEvent::Kind::Motion, .x = motion.x * density, .y = motion.y * density};
}

std::optional<InputEvent> TextEventFor(const SDL_TextInputEvent& text)
{
    if (text.text == nullptr || text.text[0] == '\0')
    {
        return std::nullopt;
    }
    InputEvent event{.kind = InputEvent::Kind::Text};
    event.text = std::string{text.text};
    return event;
}

std::optional<InputEvent> WheelEventFor(const SDL_MouseWheelEvent& wheel)
{
    // Negar el mínimo de un int32 es UB; nadie gira tanto en un evento.
    if (wheel.integer_y == std::numeric_limits<std::int32_t>::min())
    {
        return std::nullopt;
    }
    const std::int32_t notches = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -wheel.integer_y : wheel.integer_y;
    if (notches == 0)
    {
        return std::nullopt;
    }
    return InputEvent{.kind = InputEvent::Kind::Wheel, .wheel = static_cast<float>(notches)};
}
} // namespace

std::optional<Input::Key> KeyFor(SDL_Scancode scancode)
{
    // SDL tiene las letras de la A a la Z, los dígitos del 1 al 9 y después el 0, y las F de la 1 a
    // la 12, cada grupo seguido (SDL_scancode.h).
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return KeyAfter(Key::A, scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    {
        return KeyAfter(Key::Digit1, scancode - SDL_SCANCODE_1);
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
    {
        return KeyAfter(Key::F1, scancode - SDL_SCANCODE_F1);
    }
    switch (scancode)
    {
    case SDL_SCANCODE_0:
        return Key::Digit0;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_RETURN:
        return Key::Enter;
    case SDL_SCANCODE_PAGEUP:
        return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return Key::PageDown;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_PRINTSCREEN:
        return Key::PrintScreen;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_LSHIFT:
        return Key::ShiftLeft;
    case SDL_SCANCODE_RSHIFT:
        return Key::ShiftRight;
    case SDL_SCANCODE_LCTRL:
        return Key::CtrlLeft;
    case SDL_SCANCODE_RCTRL:
        return Key::CtrlRight;
    case SDL_SCANCODE_LALT:
        return Key::AltLeft;
    case SDL_SCANCODE_RALT:
        return Key::AltRight;
    default:
        return std::nullopt;
    }
}

std::optional<Input::InputEvent> InputEventFor(const SDL_Event& event, float pixelDensity)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        return KeyEventFor(event.key, InputEvent::Kind::KeyDown);
    case SDL_EVENT_KEY_UP:
        return KeyEventFor(event.key, InputEvent::Kind::KeyUp);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return ButtonEventFor(event.button, InputEvent::Kind::ButtonDown);
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return ButtonEventFor(event.button, InputEvent::Kind::ButtonUp);
    case SDL_EVENT_MOUSE_WHEEL:
        return WheelEventFor(event.wheel);
    case SDL_EVENT_MOUSE_MOTION:
        return MotionEventFor(event.motion, pixelDensity);
    case SDL_EVENT_TEXT_INPUT:
        return TextEventFor(event.text);
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return InputEvent{.kind = InputEvent::Kind::FocusLost};
    default:
        return std::nullopt;
    }
}
} // namespace Client::Platform
