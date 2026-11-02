#include "Platform/WindowEvents.h"

#include <SDL3/SDL_events.h>

#include <optional>

// PumpEvents: la tabla del contrato de la CL3.1, de los SDL_EVENT_* a los WindowEvent.
namespace Client::Platform
{
namespace
{
std::optional<App::WindowEvent> TranslateWindowEvent(const SDL_Event& event)
{
    // type es un Uint32 (SDL_events.h): el cast deja el switch sobre el enum, sin conversiones.
    switch (static_cast<SDL_EventType>(event.type))
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return App::WindowEvent::CloseRequested;
    case SDL_EVENT_WINDOW_MINIMIZED:
        return App::WindowEvent::Minimized;
    case SDL_EVENT_WINDOW_RESTORED:
        return App::WindowEvent::Restored;
    case SDL_EVENT_WINDOW_OCCLUDED:
        return App::WindowEvent::Occluded;
    case SDL_EVENT_WINDOW_EXPOSED:
        return App::WindowEvent::Exposed;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return App::WindowEvent::FocusLost;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return App::WindowEvent::FocusGained;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return App::WindowEvent::PixelSizeChanged;
    default:
        return std::nullopt;
    }
}
} // namespace

void PumpEvents(App::WindowState& state)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        const std::optional<App::WindowEvent> windowEvent = TranslateWindowEvent(event);
        if (!windowEvent)
        {
            continue;
        }
        // event.window es el miembro activo solo en los eventos de ventana: el tamaño se lee
        // únicamente en PixelSizeChanged.
        App::PixelSize size;
        if (*windowEvent == App::WindowEvent::PixelSizeChanged)
        {
            size = App::PixelSize{event.window.data1, event.window.data2};
        }
        state = App::Apply(state, *windowEvent, size);
    }
}
} // namespace Client::Platform
