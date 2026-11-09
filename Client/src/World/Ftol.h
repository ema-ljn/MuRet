#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

// El contrato común de 0x69fd3c (_ftol) para los callers que consumen EAX firmado (06 § Creación
// común de los objetos): trunca a signed64 y se queda con el dword bajo, con signo. Dentro del
// dominio finito [-2^63, 2^63) las dos políticas dan lo mismo; sólo difieren fuera de él. Ninguna
// reproduce el control/status x87, los traps ni los payloads NaN; −2^63 vale en las dos.
namespace Client::World
{
namespace Detail
{
// Finito y en [-2^63, 2^63). +2^63 no entra: INT64_MAX como double redondea a ese mismo límite.
[[nodiscard]] inline bool IsInFtolDomain(double value)
{
    constexpr double Signed64Limit = 0x1p63;
    return std::isfinite(value) && value >= -Signed64Limit && value < Signed64Limit;
}

// Precondición: IsInFtolDomain(value).
[[nodiscard]] inline std::int32_t FtolLowInDomain(double value)
{
    const auto whole = static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole)));
}
} // namespace Detail

// Política enmascarada: con la excepción invalid enmascarada, FISTP deja el entero indefinido
// 0x8000000000000000, cuyo low32 es 0. Por eso NaN, ±inf y lo que cae fuera del dominio dan 0.
[[nodiscard]] inline std::int32_t FtolLow(double value)
{
    return Detail::IsInFtolDomain(value) ? Detail::FtolLowInDomain(value) : 0;
}

// Política verificada: fuera del dominio, lo que sigue en el original depende de las máscaras x87,
// así que no se inventa un resultado portable y el caller corta con su propio error.
[[nodiscard]] inline std::optional<std::int32_t> FtolLowChecked(double value)
{
    if (!Detail::IsInFtolDomain(value))
    {
        return std::nullopt;
    }
    return Detail::FtolLowInDomain(value);
}
} // namespace Client::World
