#pragma once

#include "App/VSyncMode.h"

#include <string>

// El intervalo de swap de la ventana (CL3.1): cómo cada VSyncMode se le pide a SDL.
namespace Client::Platform
{
// Lo que quedó al pedir un VSyncMode.
struct SwapIntervalResult
{
    App::VSyncMode mode = App::VSyncMode::Off;
    // El SDL_GetError() del primer pedido que falló; vacío si quedó el modo pedido.
    std::string error;
};

// Las dos llamadas de SDL que usa ApplySwapInterval, con su forma, para probarla con dobles.
using SetSwapIntervalFunction = bool (*)(int interval);
using GetSwapIntervalFunction = bool (*)(int* interval);

// SDL_GL_SetSwapInterval y SDL_GL_GetSwapInterval, sobre el contexto actual.
bool SdlSetSwapInterval(int interval);
bool SdlGetSwapInterval(int* interval);

// El intervalo que se le pasa a SDL_GL_SetSwapInterval: 1, -1 o 0.
[[nodiscard]] int SwapIntervalFor(App::VSyncMode vsync);

// "On", "Adaptive" u "Off", para los avisos.
[[nodiscard]] const char* VSyncModeName(App::VSyncMode vsync);

// Pide el intervalo al contexto actual; si Adaptive falla, prueba con On (lo recomienda la
// documentación de SDL_GL_SetSwapInterval). Si nada entra, el modo es el que el contexto ya tenía
// según `get`, u Off si `get` también falla (en Xvfb fallan el 1 y el 0).
[[nodiscard]] SwapIntervalResult ApplySwapInterval(App::VSyncMode wanted,
                                                   SetSwapIntervalFunction set = SdlSetSwapInterval,
                                                   GetSwapIntervalFunction get = SdlGetSwapInterval);
} // namespace Client::Platform
