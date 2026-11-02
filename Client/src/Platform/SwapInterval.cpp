#include "Platform/SwapInterval.h"

#include <SDL3/SDL_video.h>

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
} // namespace

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
    return IntervalOn;
}

App::VSyncMode ApplySwapInterval(App::VSyncMode wanted)
{
    if (SDL_GL_SetSwapInterval(SwapIntervalFor(wanted)))
    {
        return wanted;
    }
    if (wanted == App::VSyncMode::Adaptive && SDL_GL_SetSwapInterval(IntervalOn))
    {
        return App::VSyncMode::On;
    }
    // No se pudo cambiar: queda el que el contexto ya tenía, si SDL lo sabe.
    int interval = IntervalOff;
    if (!SDL_GL_GetSwapInterval(&interval))
    {
        return App::VSyncMode::Off;
    }
    return ModeFor(interval);
}
} // namespace Client::Platform
