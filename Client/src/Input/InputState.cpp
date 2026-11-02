#include "Input/InputState.h"

#include <cstddef>
#include <optional>

namespace Client::Input
{
namespace
{
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
        m_wheel += event.wheel;
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
    if (!index || m_keysDown[*index])
    {
        return;
    }
    m_keysDown.set(*index);
    m_keysPressed.set(*index);
}

void InputState::Release(Key key)
{
    const auto index = IndexOf(key);
    if (!index || !m_keysDown[*index])
    {
        return;
    }
    m_keysDown.reset(*index);
    m_keysReleased.set(*index);
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
    // Lo que estaba abajo se suelta con su flanco, como si hubiera llegado cada KeyUp.
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
}
} // namespace Client::Input
