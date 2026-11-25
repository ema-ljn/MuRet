#pragma once

#include "Core/IClock.h"

// El reloj real del bucle (CL3.2): el IClock que Main le pasa a GameLoop, sobre el reloj de SDL.
namespace Client::Platform
{
class SdlClock final : public Core::IClock
{
public:
    // SDL_GetTicksNS: los ns desde que SDL arrancó; monótono y nunca negativo.
    Core::Duration Now() override;
    // SDL_DelayPrecise. Una duración de 0 o negativa no duerme.
    void SleepFor(Core::Duration duration) override;
};
} // namespace Client::Platform
