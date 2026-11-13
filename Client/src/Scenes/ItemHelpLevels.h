#pragma once

#include "Data/Tables/ItemTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// La tabla por nivel de la ayuda del ítem (06 § HUD y ventanas, "La ayuda del ítem"): 0x5afc00 la llena en 0x787a15c
// (14 niveles de 0x44 bytes, 17 dwords cada uno) y la guarda para el ítem de 0x6c9334. La leen la cola de 0x5b0550 (el
// ancho de la caja y qué columnas dibuja) y 0x5af9b0 (una columna en los renglones).
namespace Client::Scenes
{
struct GameHudContext;

inline constexpr std::size_t ItemHelpLevelCount = 14;
inline constexpr std::size_t ItemHelpColumnCount = 17;
// 0x6c9334 (.data −1): ningún ítem todavía.
inline constexpr std::int32_t NoItemHelpTable = -1;

// El dword de cada columna, 0x787a15c + 0x44 · nivel + 4 · columna. La 11, la 12 y la 16 no las escribe nadie: quedan
// en 0, como la .bss.
enum ItemHelpColumn : std::size_t
{
    ItemHelpLevel = 0,
    // 1 si el registro propio cumple los requisitos del nivel, 0 si no.
    ItemHelpUsable = 1,
    ItemHelpMinDamage = 2,     // Item.bmd+0x26
    ItemHelpMaxDamage = 3,     // +0x27
    ItemHelpMagic = 4,         // +0x2f, salvo de 0x408 a 0x40d
    ItemHelpScepterMagic = 5,  // +0x2f, de 0x408 a 0x40d
    ItemHelpDefense = 6,       // +0x29
    ItemHelpDefenseRate = 7,   // +0x28
    ItemHelpStrength = 8,      // +0x30
    ItemHelpAgility = 9,       // +0x32
    ItemHelpEnergy = 10,       // +0x34
    ItemHelpLeadership = 13,   // +0x38
    ItemHelpVitality = 14,     // +0x36
    ItemHelpRequiredLevel = 15 // +0x3a
};

using ItemHelpRow = std::array<std::int32_t, ItemHelpColumnCount>;

struct ItemHelpLevels
{
    // 0x787a15c: un renglón por nivel, del 0 al 13.
    std::array<ItemHelpRow, ItemHelpLevelCount> rows{};
    // 0x6c9334: el ítem de la tabla. Nadie más lo escribe: ni cerrar la ayuda ni cambiar los stats la vuelven a llenar
    // para el mismo ítem (Q-C750).
    std::int32_t item = NoItemHelpTable;
};

// 0x5afc00(ítem): con el ítem de la tabla, nada. Si no, lo guarda y llena los 14 niveles con el registro de Item.bmd
// del ítem (`record`, el de 0x77f3b0c): cada byte de +0x26, +0x27, +0x28 y +0x29 (distinto de 0) más 3 · min(nivel, 9)
// y, del 10 al 13, 4, 9, 15 y 22 más (el escudo, de 0xc00 a 0xdff, suma solo el nivel a +0x29); +0x2f igual, a la
// mitad y, salvo de 0x408 a 0x40d, más 2 · nivel; cada requisito de +0x30 a +0x38 distinto de 0, (Item.bmd+0x20 + 3 ·
// nivel) · requisito · 3 (4 la energía) / 100 + 20, en 32 bits con signo; y +0x3a tal cual. Después, con el registro
// propio abierto (3.848 rand() al cerrarlo si queda sin accesos, uno por nivel), si cumple los requisitos (los stats
// más lo de los ítems, en 16 bits, y el nivel).
void FillTheItemHelpLevels(GameHudContext& context, std::int32_t item,
                           std::span<const std::byte, Data::Tables::ItemRecordSize> record);
} // namespace Client::Scenes
