#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 hito 4, fase 2c: la posición relativa de un blanco (0x4e9920), que mandan las ramas de las clases de 0x57cfc0
// (0x580a1a, 0x58657c, 0x587b2c, 0x58891c, 0x58dd70 y 0x59409f) en el 1E.
namespace Client::Scenes
{
// 0x4e9920(px, py, tx, ty): con c = −0,01 (0x6ba110), el nibble alto ((ftol(py · c) − ftol(ty · c)) + 8) << 4 y el bajo
// (ftol(px · c) − ftol(tx · c)) + 8, en bytes (los ftol de `ty` y `py` primero). `px` y `py` son float y `tx`, `ty`
// enteros (FILD). Corta si un ftol sale de su perfil.
[[nodiscard]] Core::Expected<std::uint8_t, std::string> RelativeCellOf(float px, float py, std::int32_t tx,
                                                                       std::int32_t ty);
} // namespace Client::Scenes
