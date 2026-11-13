#pragma once

#include <cstdint>
#include <optional>

// 0x4e9920(x, y, blancoX, blancoY), CL9 hito 4, fase 2c: el byte de la celda del blanco relativa a (x, y), con cada
// coordenada por −0,01 ([0x6ba110]) y su _ftol: arriba (((y) − (blancoY)) + 8) << 4 y abajo ((x) − (blancoX)) + 8,
// unidos con OR sin enmascarar el de abajo, que fuera de −8..7 pisa el nibble de arriba (Q-C1232). Los blancos llegan
// en int (FILD). La llaman las ramas de las clases de 0x57cfc0 (0x580a1a, 0x58657c, 0x587b2c, 0x58891c, 0x58dd70 y
// 0x59409f) con la posición del héroe y el _ftol de su +0x3a8/+0x3ac. Vacío si un _ftol no tiene un resultado
// portable (World::OriginalFtolLow).
namespace Client::Scenes
{
[[nodiscard]] std::optional<std::uint8_t> RelativeTargetCell(float x, float y, std::int32_t targetX,
                                                             std::int32_t targetY);
} // namespace Client::Scenes
