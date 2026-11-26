#include "Platform/Sdl/SwapInterval.h"

#include "Platform/VSyncMode.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#include <string>

namespace Client::Platform::Sdl
{
namespace
{
// SDL_GL_SetSwapInterval's values (SDL_video.h): once per retrace, adaptive and no wait.
constexpr int IntervalOn = 1;
constexpr int IntervalAdaptive = -1;
constexpr int IntervalOff = 0;

VSyncMode ModeFor(int interval)
{
    if (interval == IntervalAdaptive)
    {
        return VSyncMode::Adaptive;
    }
    return interval == IntervalOff ? VSyncMode::Off : VSyncMode::On;
}

// Requests nothing: the interval `get` reports, or Off if it does not know.
VSyncMode CurrentMode(GetSwapIntervalFunction get)
{
    int interval = IntervalOff;
    if (!get(&interval))
    {
        return VSyncMode::Off;
    }
    return ModeFor(interval);
}
} // namespace

bool SdlSetSwapInterval(int interval)
{
    return SDL_GL_SetSwapInterval(interval);
}

bool SdlGetSwapInterval(int* interval)
{
    return SDL_GL_GetSwapInterval(interval);
}

int SwapIntervalFor(VSyncMode vsync)
{
    switch (vsync)
    {
    case VSyncMode::On:
        return IntervalOn;
    case VSyncMode::Adaptive:
        return IntervalAdaptive;
    case VSyncMode::Off:
        return IntervalOff;
    }
    // Unreachable with the three modes; GCC still wants it (-Wreturn-type), since an enum class can hold another value
    // through a cast.
    return IntervalOn;
}

const char* VSyncModeName(VSyncMode vsync)
{
    switch (vsync)
    {
    case VSyncMode::On:
        return "On";
    case VSyncMode::Adaptive:
        return "Adaptive";
    case VSyncMode::Off:
        return "Off";
    }
    // Unreachable, as in SwapIntervalFor.
    return "?";
}

SwapIntervalResult ApplySwapInterval(VSyncMode wanted, SetSwapIntervalFunction set, GetSwapIntervalFunction get)
{
    if (set(SwapIntervalFor(wanted)))
    {
        return SwapIntervalResult{wanted, {}};
    }
    // The request's error, before another call overwrites it.
    SwapIntervalResult result{VSyncMode::Off, SDL_GetError()};
    if (wanted == VSyncMode::Adaptive && set(IntervalOn))
    {
        result.mode = VSyncMode::On;
        return result;
    }
    result.mode = CurrentMode(get);
    if (result.mode == wanted)
    {
        result.error.clear();
    }
    return result;
}
} // namespace Client::Platform::Sdl
