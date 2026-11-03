#include "Platform/SdlClock.h"

#include "Core/IClock.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cstdint>

namespace Client::Platform
{
Core::Duration SdlClock::Now()
{
    // SDL_GetTicksNS devuelve Uint64; en int64 entran 292 años desde que arrancó SDL.
    return Core::Duration{static_cast<std::int64_t>(SDL_GetTicksNS())};
}

void SdlClock::SleepFor(Core::Duration duration)
{
    // SDL_DelayPrecise recibe Uint64: una duración negativa pasada así sería una espera de siglos,
    // así que se recorta antes del cast.
    if (duration <= Core::Duration::zero())
    {
        return;
    }
    // SDL_DelayPrecise espera activamente el último tramo. Con SDL_DelayNS o
    // std::this_thread::sleep_for, un despertar tarde en el modo fiel puede caer en el cuarto tick
    // de 15,625 ms y dar un frame de 62 ms en vez de 46,875.
    SDL_DelayPrecise(static_cast<Uint64>(duration.count()));
}
} // namespace Client::Platform
