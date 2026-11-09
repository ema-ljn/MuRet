#pragma once

#include "Core/Expected.h"
#include "Core/IMillisecondClock32.h"

#include <cstdint>
#include <system_error>

// Windows: timeGetTime real. Linux: CLOCK_BOOTTIME. macOS (10.12+): CLOCK_MONOTONIC_RAW.
// Linux/macOS incluyen suspensión; macOS documenta un origen arbitrario del sistema, no una
// época idéntica a Windows. Resolución y origen son del OS: no se igualan a un XP particular.
// Sin recursos ni época del proceso guardada; el dueño debe sobrevivir a sus referencias.
namespace Client::Platform
{
class SystemMillisecondClock32 final : public Core::IMillisecondClock32
{
public:
    Core::Expected<std::uint32_t, std::error_code> Sample() override;
};
} // namespace Client::Platform
