#pragma once

#include "Core/IClock.h"
#include "PlatformApi.h"

namespace Client::Platform::Sdl
{
class PLATFORM_API SdlClock final : public Core::IClock
{
public:
    // SDL_GetTicksNS: ns since SDL started; monotonic and never negative.
    Core::Duration Now() override;
    // SDL_DelayPrecise. A duration of 0 or less does not sleep.
    void SleepFor(Core::Duration duration) override;
};
} // namespace Client::Platform::Sdl
