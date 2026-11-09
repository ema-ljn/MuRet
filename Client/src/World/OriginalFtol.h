#pragma once

#include <cstdint>
#include <optional>

// El contrato común de 0x69fd3c y los callers que consumen EAX firmado (06 § Creación común de los objetos).
namespace Client::World
{
// Trunca signed64 y devuelve su dword bajo con signo, para los valores finitos en [-2^63,2^63) y para el
// NaN, que da 0: lo que 0x606660 hizo con la y en NaN de Devias (Q-C186, observado). Con un infinito o
// fuera de int64 la continuidad nativa sigue sin observar y no se inventa un resultado portable.
[[nodiscard]] std::optional<std::int32_t> OriginalFtolLow(double value);
} // namespace Client::World
