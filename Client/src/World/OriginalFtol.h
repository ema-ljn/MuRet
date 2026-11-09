#pragma once

#include <cstdint>
#include <optional>

// El contrato común de 0x69fd3c y los callers que consumen EAX firmado (06 § Creación común de los objetos).
namespace Client::World
{
// Trunca signed64 y devuelve su dword bajo con signo. Sólo valores finitos en [-2^63,2^63): fuera,
// la continuidad nativa depende de los masks x87 y no se inventa un resultado portable.
[[nodiscard]] std::optional<std::int32_t> OriginalFtolLow(double value);
} // namespace Client::World
