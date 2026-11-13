#pragma once

#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

// La mezcla de joyas del NPC del 30 con 9 (los diálogos 0xa1, 0xa2, 0xa3, 0xa4 y 0xa6, y el BC; los textos 1806 a
// 1815 de Text.bmd, "misturar" y "dissolver"): juntar 10, 20 o 30 joyas de bendición (0x1c0d) o del alma (0x1c0e)
// en un paquete (0x181e o 0x181f), o deshacer uno. Los globales de 0x6e5794..0x6e57a0 y 0x6c62d8..0x6c62e1; los de
// 0x6c62xx empiezan en 0xff en la .data, los de 0x6e57xx en 0 en la .bss. 0x402e10 los vuelve a esos valores (salvo
// 0x6e579c, que deja en −1).
struct JewelMixState
{
    // 0x6e5794: 0 juntar (el 0xa2) y 1 deshacer (el 0xa4). Lo eligen los botones del 0xa1.
    std::int32_t mode = 0;
    // 0x6e5799: por qué no se puede (2 faltan joyas, 4 no hay paquete que deshacer, 5 otro caso; 0x402bc0).
    std::uint8_t refusal = 0;
    // 0x6e579a: las joyas contadas (0x402bc0), o 1 al deshacer.
    std::uint8_t counted = 0;
    // 0x6e579c: el precio (0x402cf0): la cuenta · 50.000 al juntar y 1.000.000 al deshacer.
    std::int32_t price = 0;
    // 0x6e57a0: 100 con la mezcla posible (0x402cb2), 0 o el tipo si no. Nadie de los portados lo lee.
    std::uint8_t field6e57a0 = 0;
    // 0x6c62d8: la casilla del inventario del paquete a deshacer (0x402a90); el BC 01 le suma 12 (0x401f5a).
    std::int32_t cell = -1;
    // 0x6c62dc: el nivel del paquete a deshacer (0x402a90, 0x402ae0).
    std::int32_t level = -1;
    // 0x6c62e0: la joya, 0 bendición y 1 alma; 0xff sin elegir.
    std::uint8_t kind = 0xff;
    // 0x6c62e1: cuántas juntar (10, 20 o 30; 0xff sin elegir), con signo para el BC 00 (0x40276b).
    std::uint8_t count = 0xff;
};

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
    // 0x6c9da8 (.data): 75, 70, 65, 60, 55 y 50 hasta que el 30 con el resultado 3 las pisa.
    std::array<std::int32_t, ChaosRateCount> chaosRates{75, 70, 65, 60, 55, 50};
    // 0x7d2dc80: el estado de la mezcla. 1 la pide (0x5d9362), 2 la deja lista (0x4c5f60 y 0x4da280) y 0 la apaga
    // (abrir la caja, 0x65ac28, y el 86 con 2 o 0xb).
    std::int32_t chaosMixState = 0;
    // 0x7d2dc9d: lo pone en 0 abrir la caja del caos (0x65a94f) y lo escribe un paquete del asedio (0x4e5a28, CL9.5).
    // Con algo, el dibujo suma "+ n%" a la chance del 2 y del 8 (0x5ff2f2).
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
    // 0x78aba30: el impuesto de la tienda, en por ciento. Lo escribe el B2 1A con +4 en 2 (0x4e5521, el byte de +5;
    // CL9.5): hasta entonces, el 0 de la .bss. Lo leen el título de la tienda (0x5f4813) y el precio del tooltip
    // (0x5b1203).
    std::int32_t taxRate = 0;
    // 0x7879fc8: el teclado del almacén.
    std::array<std::uint16_t, WarehouseKeyCount> keypad{};
    // 0x78ab9fc..0x78aba06: lo que se tecleó, en 0 al barajar (0x5cb43c..0x5cb44f y 0x5ea22d..). El 83 de la
    // contraseña (0x5e7e90) manda los diez primeros.
    std::array<std::uint8_t, 11> typedNumber{};
    // 0x78aba07..0x78aba0a: la contraseña nueva del modo 4, que el OK copia (un dword de lo tecleado, 0x5e7d8b) para
    // compararla en el 5 (0x5e7e34). Nadie más la lee ni la escribe.
    std::array<std::uint8_t, 4> newPassword{};
    // 0x78abb24: la contraseña que el modo 5 aceptó (el atoi de lo tecleado, 0x5e7e56) y que el 2 manda en el 83 con
    // 1 (0x5e7ffc). Nadie más la lee ni la escribe.
    std::uint16_t warehousePassword = 0;
    // 0x78abb3a: la tecla de la caja de la contraseña apretada. La prende el clic en el paso del teclado (0x5e9ab8) y
    // la apaga soltarlo (0x5e9ac9); el dibujo (0x5e9e25) baja un punto el número de la tecla del mouse.
    std::uint8_t keypadPressed = 0;
    // 0x78abb14: lo que espera el diálogo de sí o no que abrió el ítem del cursor (.bss, 0). 1 la venta valiosa (el
    // 0x98 de 0x5df417), 2 la mezcla (el 0x98 de 0x5fe610), 3 a 6 los de la CL9.4. 0x5d8d50 lo vuelve a 0 al leer la
    // respuesta.
    std::int32_t cursorChoice = 0;
    // 0x6c932e (.data, 0xff): la respuesta de los diálogos 0x94, 0x98 y 0x99 (0x62a779) y del 0x91
    // (0x626251..0x626293): 1 sí y 2 no. 0x5d8d50 la vuelve a 0xff al leerla. Hito 7: `trade.warningByte` (el lead los
    // une al juntar las ramas).
    std::uint8_t dialogAnswer = 0xff;
    // 0x78abacc: el botón del almacén que abrió el diálogo 0x73, 0 meter zen y 1 sacarlo (0x5ea18f).
    std::int32_t warehouseMoneyMode = 0;
    // 0x77f3b6c: el número del campo del diálogo 0x73, que escribe el atoi del WndProc (0x4af4e0) sobre su EDIT
    // (0x5763504). MuRet no tiene ese EDIT (06 § El chat): queda en lo que dejó el 0x73 al abrirse. El OK del 0x91 con
    // el precio también le escribe el atoi (0x6261ea) y su Enter lo lee (0x626281). Hito 7: `typedMoney`.
    std::int32_t typedMoney = 0;
    // 0x78aba60: lo que pide el 0x91, 1 el título (el texto 1128) y 2 el precio (el 1129). Lo pone quien lo abre, la
    // tienda personal del hito 7, que lo une con su campo al juntar las ramas.
    std::int32_t field78aba60 = 0;
    // 0x78aba64: el título de la tienda personal; el OK del 0x91 copia ahí lo escrito (0x6261b8) y su Enter mira si
    // está vacío (0x626262). Hito 7: `personalShop.title`.
    std::string field78aba64{};
    // 0x6c8e78 (.data, 10) y 0x77f3b6a: el modo de la entrada sin EDIT y el campo numérico de los diálogos. Abrir el
    // 0x73 los pone en 8 y 1 (0x5ea1a5) y cerrarlo en 0x2a y 0 (0x62821f, 0x4e4831); los lee el WndProc, que MuRet no
    // tiene.
    std::int32_t inputMode = 10;
    std::uint8_t numberField = 0;
    // 0x7d2dc98: el tipo de la mezcla que manda el C1 04 86 (0x5d93f8). Lo arma 0x5fd4b0 en cada paso con la caja
    // abierta (0x5d8dda, Scenes::ScanTheChaosBox).
    std::int32_t mixKind = 0;
    // 0x7d2d378: lo que vale la caja (0x5fd120 desde 0x5d8dcd), con lo que el dibujo saca la chance de la mezcla 1.
    std::int32_t chaosBoxValue = 0;
    // 0x7d3a114 (.bss, 0): el modo de la caja que elige el diálogo 0x8e (0x62599b, el botón 0 o el 1). 0x5fd4b0 lo
    // mira y el dibujo pone su título.
    std::int32_t chaosMixMode = 0;
    // 0x6c9da0 y 0x6c9da4 (.data, 5 y 4): el ancho y el alto más grandes (Item.bmd +0x24 y +0x25) de lo que hay en la
    // caja, que 0x5fd4f0 vuelve a 1 antes de contar. Siguen a las tasas (0x6c9da8): el dibujo del 2 las lee en fila.
    std::int32_t mixWidth = 5;
    std::int32_t mixHeight = 4;
    // 0x7d2dc84: la capa del 0x1c, 3 con armas excelentes y 4 con armaduras (0x5fdbe6, 0x5fdc1d).
    std::int32_t mixCapeKind = 0;
    // 0x7d2dc88: lo que valen (0x54d0f0 en el modo 1, tope 1.000.000) los ítems de nivel 4 o más con una opción de
    // 0x50 a 0x53, la chance del 0x1c.
    std::int32_t mixExcellentValue = 0;
    // 0x7d2dc8c: el nivel + 1 de los dos ítems del 2 (0x1c11 y 0x1c12) o del 8 (0x1a10 y 0x1a11).
    std::int32_t mixLevel = 0;
    // 0x7d2dc90: la opción de las alas (0x50 o 0x51 su valor, 0x55 su valor · 4). Nadie más la lee.
    std::int32_t mixWingOption = 0;
    // 0x7d2dc94: la chance de las alas del 7 y la capa del 0x18, tope 90.
    std::int32_t mixWingRate = 0;
    // 0x7d2dc9c: la suerte (la opción 0x54) del último ítem de nivel 9 a 12 o de nivel 4 o más que la mira.
    std::uint8_t mixLuck = 0;
    // 0x7d2dca0: el precio de la mezcla, que escribe el dibujo (0x5febf0) y lee el botón de mezclar (0x5fe982).
    std::uint32_t mixPrice = 0;
    // 0x78aba34: el impuesto de la caja del caos, en por ciento. Lo escribe el B2 1A con +4 en 1 (0x4e5507 y
    // 0x4dfbb0, el byte de +5; CL9.5): hasta entonces, el 0 de la .bss. Lo leen el título de la caja (0x5fed3e), el
    // precio con impuesto (0x5b12f0) y el entrenador (0x44d89e).
    std::int32_t chaosTaxRate = 0;
    // 0x6e5798: con 0, el 30 con 9 abre el diálogo 0xa1 (0x4d259d). Es la mezcla de joyas mandada (0x4029e0 lo pone
    // en 1 con el BC; 0x402e50 y 0x402e10 lo vuelven a 0): mientras, el 30 con 9 no abre nada.
    std::uint8_t field6e5798 = 0;
    // La mezcla de joyas (los diálogos 0xa1..0xa6 y el BC, Scenes/JewelMix.h).
    JewelMixState jewels{};
    // El SetCursorPos que pidió el último 30 (0x4d28f6), con el mouse que vea la escena: NpcTalkCursorWarp. Aplicarlo
    // a la ventana es de la plataforma (CL10).
    bool cursorWarpRequested = false;
};
} // namespace Client::Scenes
