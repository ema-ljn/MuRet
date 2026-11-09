#pragma once

#include <cmath>
#include <cstdint>

// Proyección portable de 0x69fd3c: FTOL signed64 truncado y low32 firmado.
// Perfil de invalid enmascarada: el entero indefinido 0x8000000000000000 tiene low32=0.
// No reproduce el control/status x87, traps ni payloads NaN; −2^63 sigue siendo válido.
namespace Client::World
{
[[nodiscard]] inline std::int32_t FtolLow(double value)
{
    constexpr double Signed64Limit = 0x1p63;
    if (std::isnan(value) || value >= Signed64Limit || value < -Signed64Limit)
    {
        return 0;
    }
    const auto whole = static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole)));
}
} // namespace Client::World
