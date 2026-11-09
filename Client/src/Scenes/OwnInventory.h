#pragma once

#include "World/ItemState.h"

#include <array>
#include <cstddef>

// Las dos grillas del inventario propio que llena el F3 10: arreglos de ItemState de 0x48 bytes en la
// .bss, que WinMain no toca y que duran de una escena a otra. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El F3 10, el inventario". Qué muestra cada una en
// la interfaz no está leído (CL9): la segunda, la de los lugares desde el 76, podría ser la tienda
// personal (*inferido* del tamaño).
namespace Client::Scenes
{
// 0x5bc740 pone una fila cada 8 casillas: el 8 que le pasa 0x4c55a0 (0x4c5e83) para la fila y el
// `% 8` y `/ 8` fijos de 0x5bcc68..0x5bcc81 para la columna y la fila de +0x42 y +0x43.
inline constexpr std::size_t InventoryColumns = 8;
// [0x78a9bb0], la de los lugares 12 a 75: la que 0x4c55a0 vacía hasta 0x78aadec − 0x3c (0x4c56be).
inline constexpr std::size_t InventoryCellCount = 64;
// [0x78ab0b8], la de los lugares desde el 76: hasta 0x78ab9f4 − 0x3c (0x4c56db).
inline constexpr std::size_t SecondGridCellCount = 32;

struct OwnInventory
{
    // La inicialización común del F3 10 (0x4c56a8..0x4c56e0), después de cerrar el registro propio:
    // en las 96 casillas, el tipo en −1 y +0x3c (occupied) y +0x1b (flags1b) en 0, como en el equipo.
    void ClearForInventoryReply();

    std::array<World::ItemState, InventoryCellCount> cells{};
    std::array<World::ItemState, SecondGridCellCount> secondCells{};
};
} // namespace Client::Scenes
