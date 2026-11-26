#include "Platform/Sdl/SwapInterval.h"

#include "Platform/VSyncMode.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#include <string>

// SwapIntervalFor y ApplySwapInterval.
namespace Client::Platform::Sdl
{
namespace
{
// Los valores de SDL_GL_SetSwapInterval (SDL_video.h): uno por retrazo, adaptativo y sin espera.
constexpr int IntervalOn = 1;
constexpr int IntervalAdaptive = -1;
constexpr int IntervalOff = 0;

// El modo de un intervalo que ya está puesto.
VSyncMode ModeFor(int interval)
{
    if (interval == IntervalAdaptive)
    {
        return VSyncMode::Adaptive;
    }
    return interval == IntervalOff ? VSyncMode::Off : VSyncMode::On;
}

// Lo que quedó, sin pedir nada: el intervalo según `get`, u Off si no lo sabe.
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
    // Inalcanzable con los tres modos; GCC lo pide igual (-Wreturn-type), porque un enum class
    // puede tener otro valor con un cast.
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
    // Inalcanzable, como en SwapIntervalFor.
    return "?";
}

SwapIntervalResult ApplySwapInterval(VSyncMode wanted, SetSwapIntervalFunction set, GetSwapIntervalFunction get)
{
    if (set(SwapIntervalFor(wanted)))
    {
        return SwapIntervalResult{wanted, {}};
    }
    // El error del pedido, antes de que otra llamada lo pise.
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
