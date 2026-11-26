#include "Platform/Sdl/SdlClock.h"

#include "Core/IClock.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cstdint>

namespace Client::Platform::Sdl
{
Core::Duration SdlClock::Now()
{
    // SDL_GetTicksNS returns Uint64; an int64 holds 292 years since SDL started.
    return Core::Duration{static_cast<std::int64_t>(SDL_GetTicksNS())};
}

void SdlClock::SleepFor(Core::Duration duration)
{
    // SDL_DelayPrecise takes Uint64: a negative duration passed like that would be a centuries-long wait, so it is
    // clipped before the cast.
    if (duration <= Core::Duration::zero())
    {
        return;
    }
    // SDL_DelayPrecise busy-waits the last stretch. With SDL_DelayNS or std::this_thread::sleep_for, a late wake in the
    // faithful mode can land on the fourth 15.625 ms tick and give a 62 ms frame instead of 46.875.
    SDL_DelayPrecise(static_cast<Uint64>(duration.count()));
}
} // namespace Client::Platform::Sdl
