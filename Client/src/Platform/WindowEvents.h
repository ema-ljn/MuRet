#pragma once

#include "App/WindowState.h"

#include <SDL3/SDL_events.h>

#include <optional>

// De SDL_Event a App::WindowEvent (CL3.1): lo único que ve SDL del estado de la ventana.
namespace Client::Platform
{
// Cualquier ventana: los SDL_WindowID válidos no son 0 (SDL_video.h).
inline constexpr SDL_WindowID AnyWindow = 0;

// El WindowEvent de un evento de SDL, o nada si no es de la ventana `windowId` (con AnyWindow, de
// cualquiera). SDL_EVENT_QUIT no tiene ventana y cuenta siempre. No llama a SDL: se prueba con un
// SDL_Event armado a mano.
[[nodiscard]] std::optional<App::WindowEvent> WindowEventFor(const SDL_Event& event, SDL_WindowID windowId = AnyWindow);

// Vacía la cola de SDL: cada evento de ventana va a `state` con App::Apply, y los demás se
// descartan (desde la CL3.3 también recibe el InputState).
void PumpEvents(App::WindowState& state, SDL_WindowID windowId = AnyWindow);
} // namespace Client::Platform
