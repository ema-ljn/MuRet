#include "Input/DoubleClick.h"

#include "Core/IClock.h"
#include "Input/InputState.h"
#include "Input/Key.h"

namespace Client::Input
{
bool DoubleClick::Step(const InputState& input, Core::Duration now)
{
    if (m_firstPress && (input.MouseX() != m_x || input.MouseY() != m_y))
    {
        m_firstPress.reset();
    }
    if (!input.WasPressed(MouseButton::Left))
    {
        return false;
    }
    if (m_firstPress && now - *m_firstPress <= DoubleClickTime)
    {
        m_firstPress.reset();
        return true;
    }
    m_firstPress = now;
    m_x = input.MouseX();
    m_y = input.MouseY();
    return false;
}
} // namespace Client::Input
