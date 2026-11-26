#include "Platform/WindowState.h"

#include "Core/PixelSize.h"

namespace Client::Platform
{
WindowState Apply(WindowState state, WindowEvent event, Core::PixelSize size)
{
    switch (event)
    {
    case WindowEvent::CloseRequested:
        state.running = false;
        break;
    case WindowEvent::Minimized:
    case WindowEvent::Occluded:
        state.minimized = true;
        break;
    case WindowEvent::Restored:
    case WindowEvent::Exposed:
        state.minimized = false;
        break;
    case WindowEvent::FocusLost:
        state.focused = false;
        break;
    case WindowEvent::FocusGained:
        state.focused = true;
        break;
    case WindowEvent::PixelSizeChanged:
        // A side at 0 or negative is not applied: the viewport keeps the last size. SDL 3.4 does not send a 0 × 0 on
        // minimize on Windows (it returns earlier, SDL_windowsevents.c), but the check does not depend on that.
        if (size.width > 0 && size.height > 0)
        {
            state.pixelSize = size;
        }
        break;
    }
    return state;
}
} // namespace Client::Platform
