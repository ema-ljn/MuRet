#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// CL9 hito 3: los globales de 0x558bb0 que escriben otros paquetes (la guerra de gremios y el duelo, del hito 7 y del
// hito 8). Sin esos paquetes, los valores de la .bss: todo en 0.
namespace Client::Scenes
{
// 0x78abae0: el nombre del rival del duelo, que 0x558e20 compara con strcmp contra +0x1d9.
inline constexpr std::size_t DuelNameBytes = 11;

struct AttackRules
{
    // 0x576796c: con algo distinto de 0, la guerra de gremios: un monstruo se ataca sólo con 0x576796d (0x558c07), y un
    // jugador, si es del gremio rival (+0x374 en 2, 0x558e61) o, con el PK de 6 o más, si su gremio no es el del héroe
    // (0x558d8e). Qué son exactamente, sin leer: los escribe el hito 8.
    std::uint8_t byte576796c = 0;
    std::uint8_t byte576796d = 0;
    // 0x78abad8, 0x78abadc y 0x78abae0: el duelo en curso, la clave del rival (un int contra el MOVSX de +0x1f4) y su
    // nombre. Los escribe el hito 7.
    bool duelActive = false;
    std::int32_t duelKey = 0;
    std::array<std::byte, DuelNameBytes> duelName{};
};
} // namespace Client::Scenes
