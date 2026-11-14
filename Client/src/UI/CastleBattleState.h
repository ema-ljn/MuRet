#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Lo que guardan los paquetes de la batalla del castillo (CL9 hito 13; 04 § Castle Siege, la batalla): el objeto de
// 0x5767924 que arma el B2 20 y dibuja 0x5a6fc0, y la marca del gremio dueño del B9 02, que 0x5f1890 pinta en la
// textura de la bandera del 30 (0x69c, Object31\Flag.tga).
namespace Client::UI
{
// 0x4e04f3: new(0x36), dos mitades de 0x1b con un B2 20 entero cada una: la primera para la clave 0x13d y la segunda
// para las demás (0x4e051e). En cada mitad, +4/+5 la clave, +6 se muestra (0x5a71f0), +7 el bando (0x5a7215: 2, 3 o
// 4 contra los bits 0x80000, 0x400000 y 0x800000 de +0x78 del héroe), +8 un texto (8 bytes, *inferido* el gremio) y
// +0x10 otro (10 y el 0 de +0x1a, *inferido* el personaje). Los dos textos los imprime 0x5a6fc0 con %s.
inline constexpr std::size_t SwitchHoldersSize = 0x36;
inline constexpr std::size_t SwitchHolderHalf = 0x1b;
inline constexpr std::uint16_t FirstHalfKey = 0x13d;
inline constexpr std::size_t HolderShownAt = 6;
inline constexpr std::size_t HolderSideAt = 7;
inline constexpr std::size_t HolderGuildAt = 8;
inline constexpr std::size_t HolderNameAt = 0x10;
inline constexpr std::size_t HolderEndAt = 0x1a;

// B9 02 (0x4e0790): 32 bytes, dos colores de 4 bits por byte (el par arriba), 8 × 8.
inline constexpr std::size_t OwnerMarkColors = 64;

struct CastleBattleState
{
    // 0x5767924: sin objeto, std::nullopt. new no lo limpia: lo que el alta no escribe (Q-C1400) MuRet lo arranca en
    // 0. Lo libera 0x4e05f0 cuando 0x5a6fc0 ve al héroe fuera de la zona.
    std::optional<std::array<std::uint8_t, SwitchHoldersSize>> switchHolders;
    // Lo que 0x4e0790 desempaqueta para 0x5f1890(0x69c, colores, 1), y cuántas veces llegó: la textura se rehace con
    // cada B9 02 (P-C372).
    std::array<std::uint8_t, OwnerMarkColors> ownerMark{};
    std::uint32_t ownerMarkRevision = 0;
};
} // namespace Client::UI
