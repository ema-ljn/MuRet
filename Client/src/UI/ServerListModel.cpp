#include "UI/ServerListModel.h"

#include <array>
#include <map>
#include <string>
#include <utility>

namespace Client::UI
{
namespace
{
// La tabla de 25 de 0x4c1860, en la pila: el lugar de g es 23 menos esto.
constexpr std::array<int, ServerGroupCount> SlotTable{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 14, 20,
                                                      15, 16, 17, 18, 11, 12, 13, 19, 21, 22, 23, 24};
constexpr int SlotBase = 23;
// El grupo 12 va al lugar 24, y no al 3 de la tabla.
constexpr std::uint16_t LastSlotGroup = 12;

constexpr std::uint16_t CodesPerGroup = 20;

// Los textos de los nombres (0x4c1860): 0x76e4aac + 300 · g es el 540 + g.
constexpr std::size_t FirstGroupNameText = 540;
constexpr std::size_t GroupTwelveText = 559;
constexpr std::size_t GroupNineteenText = 552;
constexpr std::size_t GroupTwentyText = 1829;

// Los de la carga (0x645860): 0x230, 0x231 y 0x232.
constexpr std::size_t FullLoadText = 560;
constexpr std::size_t BusyLoadText = 561;
constexpr std::size_t FreeLoadText = 562;
constexpr std::uint8_t BusyLoad = 100;
constexpr std::uint8_t FullLoad = 128;

// 0x447c70: el 1 y el 15, salvo en el lugar 4.
constexpr int FirstNonPvpNumber = 1;
constexpr int SecondNonPvpNumber = 15;
constexpr int PvpSlot = 4;

constexpr std::size_t NonPvpTitleText = 460;
constexpr std::size_t PvpTitleText = 461;

bool HasButton(int slot)
{
    return slot >= FirstButtonSlot && slot <= LastButtonSlot;
}
} // namespace

std::optional<int> GroupSlot(std::uint16_t group)
{
    if (group >= ServerGroupCount)
    {
        return std::nullopt;
    }
    if (group == LastSlotGroup)
    {
        return LastButtonSlot;
    }
    return SlotBase - SlotTable[group];
}

ServerGroups GroupServers(std::span<const Protocol::Connection::ServerEntry> list)
{
    // Por lugar, para que salgan en el orden de la pantalla.
    std::map<int, ServerGroup> bySlot;
    ServerGroups grouped;
    for (const Protocol::Connection::ServerEntry& entry : list)
    {
        const auto group = static_cast<std::uint16_t>(entry.code / CodesPerGroup);
        const std::optional<int> slot = GroupSlot(group);
        if (!slot || !HasButton(*slot))
        {
            ++grouped.ignored;
            continue;
        }
        ServerGroup& target = bySlot[*slot];
        target.group = group;
        target.slot = *slot;
        if (target.servers.size() >= ServersPerGroup)
        {
            ++grouped.ignored;
            continue;
        }
        target.servers.push_back(entry);
    }
    for (auto& [slot, group] : bySlot)
    {
        grouped.groups.push_back(std::move(group));
    }
    return grouped;
}

std::size_t GroupNameText(std::uint16_t group)
{
    switch (group)
    {
    case 12:
        return GroupTwelveText;
    case 19:
        return GroupNineteenText;
    case 20:
        return GroupTwentyText;
    default:
        return FirstGroupNameText + group;
    }
}

int ServerNumber(std::uint16_t code)
{
    return code % CodesPerGroup + 1;
}

bool IsNonPvp(int slot, int number)
{
    return (number == FirstNonPvpNumber || number == SecondNonPvpNumber) && slot != PvpSlot;
}

std::size_t LoadText(std::uint8_t load)
{
    if (load >= FullLoad)
    {
        return FullLoadText;
    }
    return load >= BusyLoad ? BusyLoadText : FreeLoadText;
}

std::size_t LoginTitleText(int slot, int number)
{
    return IsNonPvp(slot, number) ? NonPvpTitleText : PvpTitleText;
}

std::string SingleServerLabel(std::string_view group, bool nonPvp, std::string_view load)
{
    std::string label(group);
    if (nonPvp)
    {
        label.append(NonPvpMark);
    }
    label.push_back(' ');
    label.append(load);
    return label;
}

std::string ServerLabel(std::string_view group, int number, bool nonPvp, std::string_view load)
{
    std::string label(group);
    label.push_back('-');
    label.append(std::to_string(number));
    if (nonPvp)
    {
        label.append(NonPvpMark);
    }
    label.push_back(' ');
    label.append(load);
    return label;
}
} // namespace Client::UI
