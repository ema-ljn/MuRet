#include "Platform/SwapInterval.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#include <string>

// SwapIntervalFor y ApplySwapInterval.
namespace Client::Platform
{
namespace
{
// Los valores de SDL_GL_SetSwapInterval (SDL_video.h): uno por retrazo, adaptativo y sin espera.
constexpr int IntervalOn = 1;
constexpr int IntervalAdaptive = -1;
constexpr int IntervalOff = 0;

// El modo de un intervalo que ya está puesto.
App::VSyncMode ModeFor(int interval)
{
    if (interval == IntervalAdaptive)
    {
        return App::VSyncMode::Adaptive;
    }
    return interval == IntervalOff ? App::VSyncMode::Off : App::VSyncMode::On;
}

// Lo que quedó, sin pedir nada: el intervalo según `get`, u Off si no lo sabe.
App::VSyncMode CurrentMode(GetSwapIntervalFunction get)
{
    int interval = IntervalOff;
    if (!get(&interval))
    {
        return App::VSyncMode::Off;
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

int SwapIntervalFor(App::VSyncMode vsync)
{
    switch (vsync)
    {
    case App::VSyncMode::On:
        return IntervalOn;
    case App::VSyncMode::Adaptive:
        return IntervalAdaptive;
    case App::VSyncMode::Off:
        return IntervalOff;
    }
    // Inalcanzable con los tres modos; GCC lo pide igual (-Wreturn-type), porque un enum class
    // puede tener otro valor con un cast.
    return IntervalOn;
}

const char* VSyncModeName(App::VSyncMode vsync)
{
    switch (vsync)
    {
    case App::VSyncMode::On:
        return "On";
    case App::VSyncMode::Adaptive:
        return "Adaptive";
    case App::VSyncMode::Off:
        return "Off";
    }
    // Inalcanzable, como en SwapIntervalFor.
    return "?";
}

SwapIntervalResult ApplySwapInterval(App::VSyncMode wanted, SetSwapIntervalFunction set, GetSwapIntervalFunction get)
{
    if (set(SwapIntervalFor(wanted)))
    {
        return SwapIntervalResult{wanted, {}};
    }
    // El error del pedido, antes de que otra llamada lo pise.
    SwapIntervalResult result{App::VSyncMode::Off, SDL_GetError()};
    if (wanted == App::VSyncMode::Adaptive && set(IntervalOn))
    {
        result.mode = App::VSyncMode::On;
        return result;
    }
    result.mode = CurrentMode(get);
    if (result.mode == wanted)
    {
        result.error.clear();
    }
    return result;
}
} // namespace Client::Platform
