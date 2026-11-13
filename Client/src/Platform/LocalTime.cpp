#include "Platform/LocalTime.h"

#include "Core/Expected.h"
#include "Core/LocalTime.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_time.h>

#include <string>

namespace Client::Platform
{
Core::Expected<Core::LocalTime, std::string> ReadLocalTime()
{
    SDL_Time now = 0;
    if (!SDL_GetCurrentTime(&now))
        return Core::Unexpected(std::string("SDL_GetCurrentTime: ") + SDL_GetError());
    SDL_DateTime local{};
    if (!SDL_TimeToDateTime(now, &local, true))
        return Core::Unexpected(std::string("SDL_TimeToDateTime: ") + SDL_GetError());
    return Core::LocalTime{.month = local.month, .day = local.day, .hour = local.hour, .minute = local.minute};
}
} // namespace Client::Platform
