#include "Input/InputState.h"

#include <cmath>
#include <cstddef>
#include <optional>

namespace Client::Input
{
namespace
{
// Los dos lados de una tecla lógica.
struct Sides
{
    Key left;
    Key right;
};

// El índice de la tecla en las tablas, o std::nullopt si no es una tecla (Key::Count, o un valor
// fuera del enum): esa no se guarda y nunca está abajo.
std::optional<std::size_t> IndexOf(Key key)
{
    const auto index = static_cast<std::size_t>(key);
    if (index >= KeyCount)
    {
        return std::nullopt;
    }
    return index;
}

// Lo mismo con un botón.
std::optional<std::size_t> IndexOf(MouseButton button)
{
    const auto index = static_cast<std::size_t>(button);
    if (index >= MouseButtonCount)
    {
        return std::nullopt;
    }
    return index;
}

bool IsLogical(Key key)
{
    return key == Key::Shift || key == Key::Ctrl || key == Key::Alt;
}

// La tecla lógica de un lado, o std::nullopt si la tecla no es un lado.
std::optional<Key> LogicalOf(Key side)
{
    switch (side)
    {
    case Key::ShiftLeft:
    case Key::ShiftRight:
        return Key::Shift;
    case Key::CtrlLeft:
    case Key::CtrlRight:
        return Key::Ctrl;
    case Key::AltLeft:
    case Key::AltRight:
        return Key::Alt;
    default:
        return std::nullopt;
    }
}

// Los lados de una tecla lógica (Shift, Ctrl o Alt).
Sides SidesOf(Key logical)
{
    switch (logical)
    {
    case Key::Shift:
        return {Key::ShiftLeft, Key::ShiftRight};
    case Key::Ctrl:
        return {Key::CtrlLeft, Key::CtrlRight};
    default:
        return {Key::AltLeft, Key::AltRight};
    }
}
} // namespace

void InputState::Apply(const InputEvent& event)
{
    switch (event.kind)
    {
    case InputEvent::Kind::KeyDown:
        Press(event.key);
        return;
    case InputEvent::Kind::KeyUp:
        Release(event.key);
        return;
    case InputEvent::Kind::ButtonDown:
        Press(event.button);
        return;
    case InputEvent::Kind::ButtonUp:
        Release(event.button);
        return;
    case InputEvent::Kind::Wheel:
        if (std::isfinite(event.wheel))
        {
            m_wheel += event.wheel;
        }
        return;
    case InputEvent::Kind::Motion:
        m_mouseX = event.x;
        m_mouseY = event.y;
        return;
    case InputEvent::Kind::FocusLost:
        ReleaseEverything();
        return;
    }
}

bool InputState::IsDown(Key key) const
{
    const auto index = IndexOf(key);
    return index && m_keysDown[*index];
}

bool InputState::WasPressed(Key key) const
{
    const auto index = IndexOf(key);
    return index && m_keysPressed[*index];
}

bool InputState::WasReleased(Key key) const
{
    const auto index = IndexOf(key);
    return index && m_keysReleased[*index];
}

bool InputState::IsDown(MouseButton button) const
{
    const auto index = IndexOf(button);
    return index && m_buttonsDown[*index];
}

bool InputState::WasPressed(MouseButton button) const
{
    const auto index = IndexOf(button);
    return index && m_buttonsPressed[*index];
}

bool InputState::WasReleased(MouseButton button) const
{
    const auto index = IndexOf(button);
    return index && m_buttonsReleased[*index];
}

float InputState::Wheel() const
{
    return m_wheel;
}

float InputState::MouseX() const
{
    return m_mouseX;
}

float InputState::MouseY() const
{
    return m_mouseY;
}

void InputState::ConsumeEdges()
{
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_wheel = 0.0f;
}

void InputState::Press(Key key)
{
    const auto index = IndexOf(key);
    if (!index || IsLogical(key) || m_keysDown[*index])
    {
        return;
    }
    m_keysDown.set(*index);
    m_keysPressed.set(*index);
    UpdateLogical(key);
}

void InputState::Release(Key key)
{
    const auto index = IndexOf(key);
    if (!index || IsLogical(key) || !m_keysDown[*index])
    {
        return;
    }
    m_keysDown.reset(*index);
    m_keysReleased.set(*index);
    UpdateLogical(key);
}

void InputState::Press(MouseButton button)
{
    const auto index = IndexOf(button);
    if (!index || m_buttonsDown[*index])
    {
        return;
    }
    m_buttonsDown.set(*index);
    m_buttonsPressed.set(*index);
}

void InputState::Release(MouseButton button)
{
    const auto index = IndexOf(button);
    if (!index || !m_buttonsDown[*index])
    {
        return;
    }
    m_buttonsDown.reset(*index);
    m_buttonsReleased.set(*index);
}

void InputState::ReleaseEverything()
{
    // Lo que estaba abajo se suelta con su flanco, como si hubiera llegado cada KeyUp; las teclas
    // lógicas también, porque están en las mismas tablas.
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
}

void InputState::UpdateLogical(Key side)
{
    const auto logical = LogicalOf(side);
    if (!logical)
    {
        return;
    }
    const auto [left, right] = SidesOf(*logical);
    const bool down = IsDown(left) || IsDown(right);
    const auto index = static_cast<std::size_t>(*logical);
    if (down == m_keysDown[index])
    {
        return;
    }
    m_keysDown[index] = down;
    if (down)
    {
        m_keysPressed.set(index);
    }
    else
    {
        m_keysReleased.set(index);
    }
}
} // namespace Client::Input
