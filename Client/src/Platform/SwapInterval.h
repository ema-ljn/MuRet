#pragma once

#include "App/VSyncMode.h"

// El intervalo de swap de la ventana (CL3.1): cómo cada VSyncMode se le pide a SDL.
namespace Client::Platform
{
// El intervalo que se le pasa a SDL_GL_SetSwapInterval: 1, -1 o 0.
[[nodiscard]] int SwapIntervalFor(App::VSyncMode vsync);

// Lo pide al contexto actual; si Adaptive falla, prueba con On (lo recomienda la documentación de
// SDL_GL_SetSwapInterval). Devuelve el que quedó.
[[nodiscard]] App::VSyncMode ApplySwapInterval(App::VSyncMode wanted);
} // namespace Client::Platform
