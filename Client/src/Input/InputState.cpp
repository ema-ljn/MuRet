#include "Input/InputState.h"

#include "Input/TextInputEvent.h"

#include <span>
#include <string>

#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Input/MouseMessageState.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

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

bool IsEditingKey(Key key, bool ctrlDown)
{
    switch (key)
    {
    case Key::Up:
    case Key::Down:
    case Key::Left:
    case Key::Right:
    case Key::Home:
    case Key::End:
    case Key::Delete:
    case Key::Backspace:
    case Key::Tab:
    case Key::Enter:
        return true;
    case Key::A:
    case Key::C:
    case Key::V:
    case Key::X:
        return ctrlDown;
    default:
        return false;
    }
}

TextInputEvent TextEvent(std::string_view text)
{
    return TextInputEvent{.kind = TextInputEvent::Kind::Text, .text = std::string(text)};
}

TextInputEvent KeyTextEvent(Key key, bool shiftDown, bool ctrlDown)
{
    return TextInputEvent{
        .kind = TextInputEvent::Kind::Key, .key = key, .text = {}, .shift = shiftDown, .ctrl = ctrlDown};
}

// El byte que Backspace, Tab y Enter dejan en el texto tecleado (InputEvent.h); las demás, ninguno.
std::optional<char> TypedCharOf(Key key)
{
    switch (key)
    {
    case Key::Backspace:
        return BackspaceChar;
    case Key::Tab:
        return TabChar;
    case Key::Enter:
        return EnterChar;
    default:
        return std::nullopt;
    }
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
    // 0x4b3444: el siguiente mensaje apaga el doble clic, aunque sea una tecla o un cambio de ventana.
    m_mouseMessages.doubleClick = false;
    m_doubleClickMessage = false;
    switch (event.kind)
    {
    case InputEvent::Kind::KeyDown:
        if (event.repeat)
        {
            const bool ctrlDown = IsDown(Key::Ctrl);
            if (const std::optional<char> typed = TypedCharOf(event.key))
            {
                m_text.push_back(*typed);
            }
            if (IsEditingKey(event.key, ctrlDown))
            {
                m_textInputEvents.push_back(KeyTextEvent(event.key, IsDown(Key::Shift), ctrlDown));
            }
            return;
        }
        {
            const bool wasDown = IsDown(event.key);
            Press(event.key);
            const bool ctrlDown = IsDown(Key::Ctrl);
            if (!wasDown && IsEditingKey(event.key, ctrlDown))
            {
                m_textInputEvents.push_back(KeyTextEvent(event.key, IsDown(Key::Shift), ctrlDown));
            }
        }
        return;
    case InputEvent::Kind::KeyUp:
        Release(event.key);
        return;
    case InputEvent::Kind::ButtonDown:
        Press(event.button);
        if (event.button == MouseButton::Left)
        {
            m_mouseMessages.leftPress = !m_mouseMessages.leftDown || m_mouseMessages.leftPress;
            m_mouseMessages.leftDown = true;
            m_mouseMessages.leftRelease = false;
        }
        return;
    case InputEvent::Kind::ButtonDoubleClick:
        Press(event.button);
        m_mouseMessages.doubleClick = event.button == MouseButton::Left;
        m_doubleClickMessage = m_mouseMessages.doubleClick;
        return;
    case InputEvent::Kind::ButtonUp:
        Release(event.button);
        if (event.button == MouseButton::Left)
        {
            m_mouseMessages.leftDown = false;
            m_mouseMessages.leftPress = false;
            m_mouseMessages.leftRelease = true;
        }
        return;
    case InputEvent::Kind::Wheel:
        if (std::isfinite(event.wheel))
        {
            m_wheel += event.wheel;
            m_lastWheel = event.wheel;
        }
        return;
    case InputEvent::Kind::Motion:
        m_mouseX = event.x;
        m_mouseY = event.y;
        return;
    case InputEvent::Kind::FocusLost:
        ReleaseEverything();
        m_mouseMessages = MouseMessageState{.leftRelease = WasReleased(MouseButton::Left)};
        m_text.clear();
        m_textInputEvents.clear();
        // El consumidor debe quitar el preedit aunque se descarte el resto de la entrada del pump.
        m_textInputEvents.push_back(TextInputEvent{.kind = TextInputEvent::Kind::Composition, .text = {}});
        return;
    case InputEvent::Kind::Text:
        m_text += event.text;
        if (!event.text.empty())
        {
            m_textInputEvents.push_back(TextEvent(event.text));
        }
        return;
    case InputEvent::Kind::Composition:
        m_textInputEvents.push_back(TextInputEvent{.kind = TextInputEvent::Kind::Composition,
                                                   .text = event.text,
                                                   .compositionStart = event.compositionStart,
                                                   .compositionLength = event.compositionLength});
        return;
    case InputEvent::Kind::Message:
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

bool InputState::TakePress(Key key) const
{
    const auto index = IndexOf(key);
    if (!index || !m_keysPressed[*index] || m_keysTaken[*index])
    {
        return false;
    }
    m_keysTaken.set(*index);
    return true;
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

const MouseMessageState& InputState::MouseMessages() const
{
    return m_mouseMessages;
}

std::optional<bool> InputState::TakeDoubleClickMessage() const
{
    const std::optional<bool> message = m_doubleClickMessage;
    m_doubleClickMessage.reset();
    return message;
}

float InputState::Wheel() const
{
    return m_wheel;
}

float InputState::LastWheel() const
{
    return m_lastWheel;
}

float InputState::MouseX() const
{
    return m_mouseX;
}

float InputState::MouseY() const
{
    return m_mouseY;
}

std::string_view InputState::Text() const
{
    return m_text;
}

std::span<const TextInputEvent> InputState::TextInputEvents() const
{
    return m_textInputEvents;
}

void InputState::ConsumeEdges()
{
    m_keysPressed.reset();
    m_keysReleased.reset();
    m_keysTaken.reset();
    m_buttonsPressed.reset();
    m_buttonsReleased.reset();
    m_mouseMessages.leftPress = false;
    m_mouseMessages.leftRelease = false;
    m_wheel = 0.0f;
    m_lastWheel = 0.0f;
    m_text.clear();
    m_textInputEvents.clear();
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
    if (const std::optional<char> typed = TypedCharOf(key))
    {
        m_text.push_back(*typed);
    }
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
    // lógicas también, porque están en las mismas tablas. Los apretones y la rueda de antes se
    // borran: el paso siguiente no ve un clic de antes del alt-tab (como MuMain, Winmain.cpp). Lo
    // tecleado, lo mismo.
    m_keysReleased |= m_keysDown;
    m_keysDown.reset();
    m_keysPressed.reset();
    m_keysTaken.reset();
    m_buttonsReleased |= m_buttonsDown;
    m_buttonsDown.reset();
    m_buttonsPressed.reset();
    m_wheel = 0.0f;
    m_lastWheel = 0.0f;
    m_text.clear();
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
