#pragma once

#include "Core/Expected.h"
#include "Core/LocalTime.h"
#include "PlatformApi.h"

#include <string>

// The original's GetLocalTime (0x6381c7, the screenshot name), from SDL's clock.
namespace Client::Platform::Sdl
{
// On Windows SDL takes it from the system time with the current zone (SystemTimeToTzSpecificLocalTime), what
// GetLocalTime gives (*inferred* from SDL 3.2's sources). If SDL does not know it, returns its error.
[[nodiscard]] PLATFORM_API Core::Expected<Core::LocalTime, std::string> ReadLocalTime();
} // namespace Client::Platform::Sdl
