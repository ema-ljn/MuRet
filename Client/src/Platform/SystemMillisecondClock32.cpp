#include "Platform/SystemMillisecondClock32.h"

#include "Core/Expected.h"

#include <cstdint>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <cerrno>
#include <time.h>
#endif

namespace Client::Platform
{
Core::Expected<std::uint32_t, std::error_code> SystemMillisecondClock32::Sample()
{
#if defined(_WIN32)
    // API pública, Winmm.lib: learn.microsoft.com/windows/win32/api/timeapi/nf-timeapi-timegettime.
    // No tiene un retorno de error documentado; cero también es una muestra válida.
    return static_cast<std::uint32_t>(timeGetTime());
#elif (defined(__linux__) && defined(CLOCK_BOOTTIME)) || (defined(__APPLE__) && defined(CLOCK_MONOTONIC_RAW))
#if defined(__linux__)
    // docs.kernel.org/core-api/timekeeping.html: arranque del sistema, incluye suspensión.
    constexpr clockid_t ClockId = CLOCK_BOOTTIME;
#else
    // Apple Libc, gen/clock_gettime.3: monotónico del sistema, incluye sueño, origen arbitrario.
    // CLOCK_UPTIME_RAW excluye sueño; no es un sustituto de esta política portable.
    constexpr clockid_t ClockId = CLOCK_MONOTONIC_RAW;
#endif
    timespec sampled{};
    if (clock_gettime(ClockId, &sampled) != 0)
    {
        return Core::Unexpected(std::error_code(errno, std::generic_category()));
    }
    constexpr std::uint64_t MillisecondsPerSecond = 1000;
    constexpr long NanosecondsPerMillisecond = 1000000;
    // Reducir segundos primero evita overflow en uptimes grandes. Se divide la fracción antes
    // de reducir raw32: no hay una conversión intermedia a float ni truncamiento signed32.
    const std::uint64_t secondsModulo32 = static_cast<std::uint32_t>(sampled.tv_sec);
    const auto partialMilliseconds = static_cast<std::uint64_t>(sampled.tv_nsec / NanosecondsPerMillisecond);
    return static_cast<std::uint32_t>(secondsModulo32 * MillisecondsPerSecond + partialMilliseconds);
#else
    return Core::Unexpected(std::make_error_code(std::errc::operation_not_supported));
#endif
}
} // namespace Client::Platform
