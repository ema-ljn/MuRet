#pragma once

#include "Platform/VSyncMode.h"
#include "PlatformApi.h"

#include <string>

namespace Client::Platform::Sdl
{
// What stuck after requesting a VSyncMode.
struct SwapIntervalResult
{
    VSyncMode mode = VSyncMode::Off;
    // The SDL_GetError() of the first failed request; empty if the requested mode stuck.
    std::string error;
};

// The two SDL calls ApplySwapInterval uses, so the tests can pass doubles.
using SetSwapIntervalFunction = bool (*)(int interval);
using GetSwapIntervalFunction = bool (*)(int* interval);

// On the current context.
bool SdlSetSwapInterval(int interval);
bool SdlGetSwapInterval(int* interval);

// 1, -1 or 0.
[[nodiscard]] PLATFORM_API int SwapIntervalFor(VSyncMode vsync);

[[nodiscard]] PLATFORM_API const char* VSyncModeName(VSyncMode vsync);

// If Adaptive fails, tries On (as SDL_GL_SetSwapInterval's documentation recommends). If nothing sticks, the mode is
// the one the context already had per `get`, or Off if `get` fails too (on Xvfb 1 and 0 fail).
[[nodiscard]] PLATFORM_API SwapIntervalResult ApplySwapInterval(VSyncMode wanted,
                                                                SetSwapIntervalFunction set = SdlSetSwapInterval,
                                                                GetSwapIntervalFunction get = SdlGetSwapInterval);
} // namespace Client::Platform::Sdl
