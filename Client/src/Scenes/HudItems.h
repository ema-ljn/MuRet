#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/OwnInventory.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Lo que el HUD del juego (CL8 hito 2) lee de los ítems: las ranuras rápidas (Q, W y E: 0x558110 y 0x558490), la
// cuenta de flechas o virotes del inventario (0x557d80) y la durabilidad máxima de un ítem (0x5b1ba0). Las grillas
// son las 64 casillas de 0x78a9bb0, que no están en la tabla de protección: recorrerlas no saca rand().
namespace Client::Scenes
{
// Las ranuras rápidas: 0x6c8e40, 0x6c8e44 y 0x6c8e48 (.data 0x1c00, 0x1c04 y 0x1c08: la poción de vida chica, la
// de maná chica y el antídoto) y sus niveles en 0x77f3b34, 0x77f3b38 y 0x77f3b3c (.bss 0). Los cambia el F3 30
// (0x4dd570, hito 3).
struct QuickSlots
{
    std::array<std::int32_t, 3> types{0x1c00, 0x1c04, 0x1c08};
    std::array<std::int32_t, 3> levels{};
};

// 0x558110(i): la casilla del inventario (fila · 8 + columna) del ítem que usa la ranura `slot`, o vacío. Con i de 0 a
// 2, el tipo de la ranura elige un rango de tipos: 0x1c08 a 0x1c0a y 0x1c14 (la ranura 2 desde 0x1c09) son ellos
// mismos, el 0x1c07 también, los de 0x1c00 a 0x1c03 y los de 0x1c04 a 0x1c06 van juntos, y cualquier otro cae en el
// rango propio de la ranura (0 y 1: el de vida y el de maná, 2: el 0x1c08). Con 3 (0x558120, las skills sin maná:
// ManaPotionSlot) los de maná, de 0x1c04 a 0x1c06, y con 4 (0x558134) los de vida, de 0x1c00 a 0x1c03, sin mirar
// las ranuras ni 0x576794c (0x55815d salta a 0x5582c1); su nivel, [0x77f3b40] y [0x77f3b44], no se lee con esos
// tipos. Con otro i, el rango sale de locales sin inicializar: el error. Recorre los tipos de arriba abajo y,
// en cada uno, las columnas de la 7 a la 0 y en cada columna las filas de la 7 a la 0: la primera casilla con ese
// tipo y +0x3c mayor que 0 gana, salvo que su nivel ((D+4 >> 3) & 0xf) no sea el de la ranura en los tipos 0x1c14,
// 0x1c0a y 0x1c07. Con `requestBlocked` (0x576794c) y la ranura en 0x1c0a, vacío (0x5582c1). Un 0x1c14 con nivel
// distinto de 0 recorre desde el número de la ranura hasta un local sin inicializar (0x558191, 0x558221 y
// 0x55827f): el error.
inline constexpr int ManaPotionSlot = 3;
inline constexpr int LifePotionSlot = 4;

[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
FindQuickSlotItem(const QuickSlots& slots, std::span<const World::ItemState, InventoryCellCount> cells, int slot,
                  bool requestBlocked);

// 0x558490(i) con i de 0 a 2: cuántos hay para la ranura `slot`, con los mismos rangos y el mismo recorrido que
// FindQuickSlotItem, salvo que la ranura 2 con un tipo fuera de los rangos cae en el 0x1c08 y que no mira
// 0x576794c. Los 0x1c09, 0x1c0a y 0x1c14 cuentan casillas; los demás suman la durabilidad (D+0x1a), que en las
// pociones es la cantidad. El 0x1c14 con nivel es el mismo error (0x5584ec, 0x55858b y 0x5585fa).
[[nodiscard]] Core::Expected<std::int32_t, std::string>
CountQuickSlotItems(const QuickSlots& slots, std::span<const World::ItemState, InventoryCellCount> cells, int slot);

// 0x557d80 después de leer el registro propio: con la clase (+0xb & 7) distinta de 2 (el elfo), 0. Si no, el tipo
// de las flechas o de los virotes que pide el arma: un arco a la izquierda (0x800 a 0x806, 0x811, 0x814 o 0x815),
// las flechas (0x80f); si no, una ballesta a la derecha (0x808 a 0x80e o 0x810 a 0x9ff), los virotes (0x807); si
// no, ninguno. Cuenta las casillas del inventario con ese tipo y +0x3c mayor que 0.
[[nodiscard]] std::int32_t CountTheAmmo(std::uint8_t characterClass, std::uint16_t rightHandType,
                                        std::uint16_t leftHandType,
                                        std::span<const World::ItemState, InventoryCellCount> cells);

// 0x5b1ba0(ítem, registro de Item.bmd, nivel): la durabilidad de Item.bmd (+0x2d, o +0x2e de 0xa00 a 0xbff) más
// lo de cada nivel (1 hasta el 4, 2 hasta el 9 y 3, 4, 5 y 6 en el 9, el 10, el 11 y del 12 en adelante), más 20
// con D+0x1c & 3 en 1 o 2, o 15 con alguna opción excelente (D+0x1b & 0x3f) fuera de 0x1803..0x1806, 0x13, 0x812,
// 0xa0a, 0x1a1e y 0x40d. En 16 bits, como el ax que devuelve.
[[nodiscard]] std::uint16_t MaxDurability(const World::ItemState& item,
                                          std::span<const std::byte, Data::Tables::ItemRecordSize> record, int level);

// El nombre de un registro de Item.bmd: sus bytes desde el +0 hasta el primer '\0' (o los 0x54), como lo lee el
// "%s" del original.
[[nodiscard]] std::string ItemRecordName(std::span<const std::byte, Data::Tables::ItemRecordSize> record);
} // namespace Client::Scenes
