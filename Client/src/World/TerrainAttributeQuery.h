#pragma once

#include "Core/Expected.h"

#include <cstdint>

namespace Client::World
{
class ITerrainAttributeQuerySource;

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
