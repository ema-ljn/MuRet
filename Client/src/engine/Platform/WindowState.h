#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"

#include <cstdint>

// Platform translates SDL's events to WindowEvent, and Apply, which is pure, says what each one changes: so it is
// tested without a window.
namespace Client::Platform
{
enum class WindowEvent : std::uint8_t
{
    // SDL_EVENT_QUIT or SDL_EVENT_WINDOW_CLOSE_REQUESTED: the original's WM_DESTROY.
    CloseRequested,
    Minimized,
    Restored,
    // Covered by another window: not drawn, like minimized.
    Occluded,
    Exposed,
    FocusLost,
    FocusGained,
    // The drawing area's size in pixels changed: the viewport follows it.
    PixelSizeChanged,
};

struct WindowState
{
    bool running = true;
    // Also when covered: not drawn, and the logic goes on (CL3.2).
    bool minimized = false;
    bool focused = true;
    Core::PixelSize pixelSize;
};

// `size` only counts with PixelSizeChanged, and one with a side at 0 or negative does not touch the size: an empty
// area is not a viewport.
[[nodiscard]] ENGINE_API WindowState Apply(WindowState state, WindowEvent event, Core::PixelSize size = {});
} // namespace Client::Platform
