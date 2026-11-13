#include "Scenes/HudItems.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/OwnInventory.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t HealingFirst = 0x1c00;
constexpr std::int32_t HealingLast = 0x1c03;
constexpr std::int32_t ManaFirst = 0x1c04;
constexpr std::int32_t ManaLast = 0x1c06;
constexpr std::int32_t ByLevel = 0x1c07;
constexpr std::int32_t Antidote = 0x1c08;
constexpr std::int32_t Counted1c09 = 0x1c09;
constexpr std::int32_t Counted1c0a = 0x1c0a;
constexpr std::int32_t Counted1c14 = 0x1c14;
constexpr std::size_t GridSide = 8;

// Los tipos que la ranura recorre, de `last` a `first`, y si cuenta casillas (0x558490) en vez de sumar.
struct SlotRange
{
    std::int32_t first = 0;
    std::int32_t last = 0;
    bool countsCells = false;
};

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// La elección de 0x558110 (0x558149..0x5582be) y de 0x558490 (0x5584a1..0x55862c), que dan los mismos rangos.
Core::Expected<SlotRange, std::string> RangeOf(const QuickSlots& slots, int slot, const char* function)
{
    if (slot < 0 || slot > 2)
        return Core::Unexpected(
            std::format("{}({}): sin ranura, el rango sale de locales sin inicializar", function, slot));
    const std::int32_t type = slots.types[static_cast<std::size_t>(slot)];
    // La ranura 2 empieza en 0x1c09: su 0x1c08 cae en el rango propio, que es el mismo.
    const std::int32_t ownFirst = slot == 2 ? Counted1c09 : Antidote;
    if (InRange(type, ownFirst, Counted1c0a) || type == Counted1c14)
    {
        if (type == Counted1c14 && slots.levels[static_cast<std::size_t>(slot)] != 0)
            return Core::Unexpected(
                std::format("{}({}): el 0x1c14 con nivel recorre hasta un local sin inicializar", function, slot));
        return SlotRange{.first = type, .last = type, .countsCells = type != Antidote};
    }
    if (slot != 0 && InRange(type, HealingFirst, HealingLast))
        return SlotRange{.first = HealingFirst, .last = HealingLast};
    if (slot != 1 && InRange(type, ManaFirst, ManaLast))
        return SlotRange{.first = ManaFirst, .last = ManaLast};
    if (type == ByLevel)
        return SlotRange{.first = type, .last = type};
    switch (slot)
    {
    case 0:
        return SlotRange{.first = HealingFirst, .last = HealingLast};
    case 1:
        return SlotRange{.first = ManaFirst, .last = ManaLast};
    default:
        return SlotRange{.first = Antidote, .last = Antidote};
    }
}

std::int32_t LevelOf(const World::ItemState& item)
{
    return static_cast<std::int32_t>((item.scalar1Low >> 3) & 0xfu);
}

// 0x5582fd..0x558350 y 0x558650..0x5586a3: el tipo con signo, +0x3c mayor que 0 y el nivel de la ranura en los que
// lo piden.
bool Matches(const World::ItemState& item, std::int32_t type, std::int32_t level)
{
    const auto id = static_cast<std::int16_t>(item.id);
    if (id != type || static_cast<std::int32_t>(item.occupied) <= 0)
        return false;
    if ((id == Counted1c14 || id == Counted1c0a) && LevelOf(item) != level)
        return false;
    return id != ByLevel || LevelOf(item) == level;
}

// 0x5582e5..0x55838e: los tipos de arriba abajo, las columnas de la 7 a la 0 y, en cada una, las filas de la 7 a la 0.
std::optional<std::size_t> FindInTheGrid(std::span<const World::ItemState, InventoryCellCount> cells,
                                         const SlotRange& range, std::int32_t level)
{
    for (std::int32_t type = range.last; type >= range.first; --type)
    {
        for (std::size_t column = GridSide; column-- > 0;)
        {
            for (std::size_t row = GridSide; row-- > 0;)
            {
                const std::size_t index = row * GridSide + column;
                if (Matches(cells[index], type, level))
                    return index;
            }
        }
    }
    return std::nullopt;
}
} // namespace

Core::Expected<std::optional<std::size_t>, std::string>
FindQuickSlotItem(const QuickSlots& slots, std::span<const World::ItemState, InventoryCellCount> cells, int slot,
                  bool requestBlocked)
{
    // 0x558119..0x558141: las pociones de las skills, sin mirar la ranura; 0x55815d las lleva a 0x5582c1.
    if (slot == ManaPotionSlot || slot == LifePotionSlot)
    {
        const SlotRange potions = slot == ManaPotionSlot ? SlotRange{.first = ManaFirst, .last = ManaLast}
                                                         : SlotRange{.first = HealingFirst, .last = HealingLast};
        return FindInTheGrid(cells, potions, 0);
    }
    auto range = RangeOf(slots, slot, "0x558110");
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    // 0x5582c1: con el pedido trabado, el 0x1c0a no se usa.
    if (requestBlocked && range.Value().last == Counted1c0a && range.Value().first == Counted1c0a)
        return std::optional<std::size_t>{};
    return FindInTheGrid(cells, range.Value(), slots.levels[static_cast<std::size_t>(slot)]);
}

Core::Expected<std::int32_t, std::string>
CountQuickSlotItems(const QuickSlots& slots, std::span<const World::ItemState, InventoryCellCount> cells, int slot)
{
    auto range = RangeOf(slots, slot, "0x558490");
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    const std::int32_t level = slots.levels[static_cast<std::size_t>(slot)];
    std::int32_t count = 0;
    for (std::int32_t type = range.Value().last; type >= range.Value().first; --type)
    {
        for (const World::ItemState& item : cells)
        {
            if (!Matches(item, type, level))
                continue;
            count += range.Value().countsCells ? 1 : static_cast<std::int32_t>(item.durability);
        }
    }
    return count;
}

std::int32_t CountTheAmmo(std::uint8_t characterClass, std::uint16_t rightHandType, std::uint16_t leftHandType,
                          std::span<const World::ItemState, InventoryCellCount> cells)
{
    constexpr std::uint8_t Elf = 2;
    constexpr std::int32_t Arrows = 0x80f;
    constexpr std::int32_t Bolts = 0x807;
    if ((characterClass & 7u) != Elf)
        return 0;
    const auto left = static_cast<std::int32_t>(static_cast<std::int16_t>(leftHandType));
    const auto right = static_cast<std::int32_t>(static_cast<std::int16_t>(rightHandType));
    std::optional<std::int32_t> ammo;
    if (InRange(left, 0x800, 0x806) || left == 0x811 || left == 0x814 || left == 0x815)
        ammo = Arrows;
    else if (InRange(right, 0x808, 0x80e) || InRange(right, 0x810, 0x9ff))
        ammo = Bolts;
    // Sin arma de las dos, el tipo buscado es el puntero del registro (local_20): ninguna casilla lo tiene.
    if (!ammo)
        return 0;
    std::int32_t count = 0;
    for (const World::ItemState& item : cells)
    {
        if (static_cast<std::int16_t>(item.id) == *ammo && static_cast<std::int32_t>(item.occupied) > 0)
            ++count;
    }
    return count;
}

std::uint16_t MaxDurability(const World::ItemState& item,
                            std::span<const std::byte, Data::Tables::ItemRecordSize> record, int level)
{
    constexpr std::size_t DurabilityAt = 0x2d;
    constexpr std::size_t StaffDurabilityAt = 0x2e;
    const auto type = static_cast<std::int16_t>(item.id);
    const bool staff = type >= 0xa00 && type < 0xc00;
    auto durability =
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(record[staff ? StaffDurabilityAt : DurabilityAt]));
    for (int step = 0; step < level; ++step)
    {
        if (step >= 12)
            durability = static_cast<std::uint16_t>(durability + 6);
        else if (step >= 11)
            durability = static_cast<std::uint16_t>(durability + 5);
        else if (step >= 10)
            durability = static_cast<std::uint16_t>(durability + 4);
        else if (step >= 9)
            durability = static_cast<std::uint16_t>(durability + 3);
        else if (step >= 4)
            durability = static_cast<std::uint16_t>(durability + 2);
        else
            durability = static_cast<std::uint16_t>(durability + 1);
    }
    const unsigned ancient = item.flags1c & 3u;
    if (ancient == 1 || ancient == 2)
        return static_cast<std::uint16_t>(durability + 0x14);
    const bool excellent = (item.flags1b & 0x3fu) != 0;
    const bool excluded = (type >= 0x1803 && type <= 0x1806) || type == 0x13 || type == 0x812 || type == 0xa0a ||
                          type == 0x1a1e || type == 0x40d;
    if (excellent && !excluded)
        return static_cast<std::uint16_t>(durability + 0xf);
    return durability;
}

std::string ItemRecordName(std::span<const std::byte, Data::Tables::ItemRecordSize> record)
{
    std::string name;
    for (const std::byte value : record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return name;
}
} // namespace Client::Scenes
