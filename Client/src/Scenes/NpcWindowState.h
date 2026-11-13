#pragma once

#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Las grillas y los globales de las ventanas de los NPC (CL9 hito 6): la tienda y el almacén comparten la grilla de
// 0x78a7070, la caja del caos tiene la suya en 0x7d2d380, y el 30 (0x4d1e00), el 31 (0x4c5f60), el 83 y el 86
// (0x4da280) escriben el resto. Todo en la .bss, que nadie vacía al cambiar de escena. Contrato:
// Client/docs/research/04-protocolo.md, "Las ventanas de los NPC".
namespace Client::Scenes
{
// 0x78a7070..0x78a926c: 120 casillas de 0x48 bytes, 8 por fila (0x5bc150 y la vuelta de 0x657ab0(0x40)).
inline constexpr std::size_t NpcGridCellCount = 120;
// 0x7d2d380..0x7d2dcbc: 32 casillas, 8 por fila y 4 filas (0x4c5f60 y 0x4da280 la llenan con 0x5bc740(.., 8, 4, ..)).
inline constexpr std::size_t ChaosBoxCellCount = 32;
inline constexpr std::int32_t NpcGridColumns = 8;
inline constexpr std::int32_t ChaosBoxRows = 4;
// 0x6c9da8..0x6c9dbc: las seis tasas de la caja del caos que copia el 30 con el resultado 3 (0x4d253b..0x4d2566).
inline constexpr std::size_t ChaosRateCount = 6;

// 0x78ab9bc..0x78ab9c8: lo que el almacén dejó esperando la contraseña, que el 83 con 0xc manda (0x4e4744..). Con el
// primero en −1, el zen (0x4e6f80 con el segundo y el tercero); si no, un ítem (0x449d80 con los cuatro).
struct PendingWarehouseMove
{
    std::int32_t slot = 0;
    std::int32_t second = 0;
    std::int32_t third = 0;
    std::int32_t fourth = 0;
};

// El SetCursorPos de 0x4d28a9..0x4d28f6, en píxeles de la pantalla.
struct CursorWarp
{
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct NpcWindowState
{
    // 0x78a7070: la grilla de la tienda de NPC y del almacén.
    std::array<World::ItemState, NpcGridCellCount> grid{};
    // 0x7d2d380: la grilla de la caja del caos.
    std::array<World::ItemState, ChaosBoxCellCount> chaosBox{};
    std::array<std::int32_t, ChaosRateCount> chaosRates{};
    // 0x7d2dc80: el estado de la mezcla. 1 la pide (0x5d9362), 2 la deja lista (0x4c5f60 y 0x4da280) y 0 la apaga
    // (abrir la caja, 0x65ac28, y el 86 con 2 o 0xb).
    std::int32_t chaosMixState = 0;
    // 0x7d2dc9d: lo pone en 0 abrir la caja del caos (0x65a94f).
    std::uint8_t field7d2dc9d = 0;
    // 0x6c932f: la traba del almacén. La escriben el 83 con 0 y 1 y la apagan abrirlo y cerrarlo.
    std::uint8_t warehouseLocked = 0;
    // 0x78abb1c: lo pone en 0 el 83 con 0 y 1, y el 83 con 0xc solo manda con él en 0.
    std::uint8_t field78abb1c = 0;
    PendingWarehouseMove pendingMove{};
    // 0x5767950: el 32 lo vuelve a 0 (0x4e3c58); con otro valor, la tienda no compra (0x5c4b91, 0x5c4bcc).
    std::int32_t buyLock = 0;
    // 0x6e5798: con 0, el 30 con 9 abre el diálogo 0xa1 (0x4d259d).
    std::uint8_t field6e5798 = 0;
    // El SetCursorPos que pidió el último 30 (0x4d28f6), con el mouse que vea la escena: NpcTalkCursorWarp. Aplicarlo
    // a la ventana es de la plataforma (CL10).
    bool cursorWarpRequested = false;
};
} // namespace Client::Scenes
