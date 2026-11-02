#pragma once

#include "App/WindowState.h"

// De SDL_Event a App::WindowEvent (CL3.1): lo único que ve SDL del estado de la ventana.
namespace Client::Platform
{
// Vacía la cola de SDL: cada evento de ventana va a `state` con App::Apply, y los demás se
// descartan (desde la CL3.3 también recibe el InputState).
void PumpEvents(App::WindowState& state);
} // namespace Client::Platform
