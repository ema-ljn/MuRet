#include "Platform/Sdl/WindowEvents.h"

#include "Platform/WindowState.h"
#include "Core/PixelSize.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Platform/Sdl/InputEvents.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <optional>

namespace Client::Platform::Sdl
{
namespace
{
bool IsWindowEvent(const SDL_Event& event)
{
    // type is a Uint32: both enum ends cast to that same type (MSVC warns on a signed/unsigned mix).
    return event.type >= static_cast<Uint32>(SDL_EVENT_WINDOW_FIRST) &&
           event.type <= static_cast<Uint32>(SDL_EVENT_WINDOW_LAST);
}
} // namespace

std::optional<WindowEvent> WindowEventFor(const SDL_Event& event, SDL_WindowID windowId)
{
    // event.window is the active member only in window events.
    if (windowId != AnyWindow && IsWindowEvent(event) && event.window.windowID != windowId)
    {
        return std::nullopt;
    }
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return WindowEvent::CloseRequested;
    case SDL_EVENT_WINDOW_MINIMIZED:
        return WindowEvent::Minimized;
    case SDL_EVENT_WINDOW_RESTORED:
        return WindowEvent::Restored;
    case SDL_EVENT_WINDOW_OCCLUDED:
        return WindowEvent::Occluded;
    case SDL_EVENT_WINDOW_EXPOSED:
        return WindowEvent::Exposed;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return WindowEvent::FocusLost;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return WindowEvent::FocusGained;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return WindowEvent::PixelSizeChanged;
    default:
        return std::nullopt;
    }
}

void PumpEvents(WindowState& state, Input::InputState& input, SDL_WindowID windowId, float pixelDensity)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (const std::optional<Input::InputEvent> inputEvent = InputEventFor(event, pixelDensity))
        {
            input.Apply(*inputEvent);
        }
        else
            input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::Message});
        const std::optional<WindowEvent> windowEvent = WindowEventFor(event, windowId);
        if (!windowEvent)
        {
            continue;
        }
        // event.window is the active member only in window events: the size is read only on PixelSizeChanged.
        Core::PixelSize size;
        if (*windowEvent == WindowEvent::PixelSizeChanged)
        {
            size = Core::PixelSize{event.window.data1, event.window.data2};
        }
        state = Apply(state, *windowEvent, size);
    }
}
} // namespace Client::Platform::Sdl
