#pragma once

#include <cstdint>

// The original does not request VSync: wglSwapIntervalEXT only appears in two stubs nobody calls (0x69e830 and
// 0x69e860). Outside Platform/Sdl so the config reads it without depending on SDL.
namespace Client::Platform
{
enum class VSyncMode : std::uint8_t
{
    // SDL_GL_SetSwapInterval(1): one swap per monitor retrace.
    On,
    // SDL_GL_SetSwapInterval(-1): like On, but a late frame does not wait for the next retrace.
    Adaptive,
    // SDL_GL_SetSwapInterval(0): no wait. The faithful mode's, like the original.
    Off,
};
} // namespace Client::Platform
