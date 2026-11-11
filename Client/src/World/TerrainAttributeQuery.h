#pragma once

#include "Core/Expected.h"

#include <bit>
#include <cstdint>

namespace Client::World
{
class ITerrainAttributeQuerySource;

// 0x6ba110: el −0,01f de los dos fmul de 0x5f82c0. Es negativo: la fila y la columna salen negativas y
// 0x7d0d348 − índice · 2 cae dentro de la tabla. Con +0,01 sólo la celda (0, 0) queda representada.
inline constexpr float TerrainAttributeQueryScale = std::bit_cast<float>(0xbc23d70au);

enum class TerrainAttributeQueryError
{
    UnrepresentedAddress
};

// Proyección EAX/word de 0x5f82c0 (contrato cerrado): PC53/RN, invalid enmascarada.
// Requiere binary32/binary64, RN del host, sin fast-math ni contracción; no reproduce
// EDX, flags, status/traps ni otros PC/RC x87. No configura el entorno FP del proceso.
// El error marca una dirección final fuera de la tabla prestada; no es una guarda nativa.
[[nodiscard]] Core::Expected<std::uint32_t, TerrainAttributeQueryError>
TerrainAttributeQueryAt(ITerrainAttributeQuerySource& source, float x, float y);
} // namespace Client::World
