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

// 0x7879fc8..0x7879fdc: las diez teclas del teclado del almacén, words con 0 a 9 que se barajan al abrirlo.
inline constexpr std::size_t WarehouseKeyCount = 10;

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
    // 0x5767950: el 32 lo vuelve a 0 (0x4e3c58); con otro valor, la tienda no compra (0x5c4b91, 0x5c4bcc). Comprar le
    // pone el precio de 0x54d0f0 en el modo 0 (0x5c4bc9 y las salidas del envío).
    std::int32_t buyLock = 0;
    // 0x78ababc: lo que cuesta reparar todo. 0x5b1570 lo cuenta en cada paso de la tienda con 0x78abb09 y lo dibuja
    // 0x5f4720.
    std::int32_t repairAllPrice = 0;
    // 0x7879fc8: el teclado del almacén.
    std::array<std::uint16_t, WarehouseKeyCount> keypad{};
    // 0x78ab9fc..0x78aba06: lo que se tecleó, en 0 al barajar (0x5cb43c..0x5cb44f y 0x5ea22d..). El 83 de la
    // contraseña (0x5e7e90) manda los diez primeros.
    std::array<std::uint8_t, 11> typedNumber{};
    // 0x78abb14: lo que espera el diálogo de sí o no que abrió el ítem del cursor (.bss, 0). 1 la venta valiosa (el
    // 0x98 de 0x5df417), 2 la mezcla (el 0x98 de 0x5fe610), 3 a 6 los de la CL9.4. 0x5d8d50 lo vuelve a 0 al leer la
    // respuesta.
    std::int32_t cursorChoice = 0;
    // 0x6c932e (.data, 0xff): la respuesta de los diálogos 0x94, 0x98 y 0x99 (0x62a779): 1 sí y 2 no. 0x5d8d50 la
    // vuelve a 0xff al leerla.
    std::uint8_t dialogAnswer = 0xff;
    // 0x78abacc: el botón del almacén que abrió el diálogo 0x73, 0 meter zen y 1 sacarlo (0x5ea18f).
    std::int32_t warehouseMoneyMode = 0;
    // 0x77f3b6c: el número del campo del diálogo 0x73, que escribe el atoi del WndProc (0x4af4e0) sobre su EDIT
    // (0x5763504). MuRet no tiene ese EDIT (06 § El chat): queda en lo que dejó el 0x73 al abrirse.
    std::int32_t typedMoney = 0;
    // 0x6c8e78 (.data, 10) y 0x77f3b6a: el modo de la entrada sin EDIT y el campo numérico de los diálogos. Abrir el
    // 0x73 los pone en 8 y 1 (0x5ea1a5) y cerrarlo en 0x2a y 0 (0x62821f, 0x4e4831); los lee el WndProc, que MuRet no
    // tiene.
    std::int32_t inputMode = 10;
    std::uint8_t numberField = 0;
    // 0x7d2dc98: el tipo de la mezcla que manda el C1 04 86 (0x5d93f8). Lo arma 0x5fd4b0 con la caja abierta
    // (0x5d8dda), que es de la fase 3 (08 P-C192): hasta entonces, el 0 de la .bss.
    std::int32_t mixKind = 0;
    // 0x6e5798: con 0, el 30 con 9 abre el diálogo 0xa1 (0x4d259d).
    std::uint8_t field6e5798 = 0;
    // El SetCursorPos que pidió el último 30 (0x4d28f6), con el mouse que vea la escena: NpcTalkCursorWarp. Aplicarlo
    // a la ventana es de la plataforma (CL10).
    bool cursorWarpRequested = false;
};
} // namespace Client::Scenes
