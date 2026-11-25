#pragma once

#include <cstdint>

// Cómo se pide el swap de la ventana (CL3.1). El original no pide VSync: wglSwapIntervalEXT solo
// aparece en dos stubs que nadie llama (0x69e830 y 0x69e860). Está en App y no en Platform porque
// la config (CL3.4) lo lee sin depender de SDL.
namespace Client::App
{
enum class VSyncMode : std::uint8_t
{
    // SDL_GL_SetSwapInterval(1): un swap por retrazo del monitor.
    On,
    // SDL_GL_SetSwapInterval(-1): como On, pero un frame que llega tarde no espera al próximo.
    Adaptive,
    // SDL_GL_SetSwapInterval(0): sin espera. Es el del modo fiel, como el original.
    Off,
};
} // namespace Client::App
