#include "FrontEnd/ServerListModel.h"

#include "Protocol/Session/ServerList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::FrontEnd
{
namespace
{
// The table of 25 of 0x4c1860, on the stack: g's slot is 23 minus this.
constexpr std::array<int, ServerGroupCount> SlotTable{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 14, 20,
                                                      15, 16, 17, 18, 11, 12, 13, 19, 21, 22, 23, 24};
constexpr int SlotBase = 23;
// Group 12 goes to slot 24, not to the table's 3.
constexpr std::uint16_t LastSlotGroup = 12;

constexpr std::uint16_t CodesPerGroup = 20;

// The name texts (0x4c1860): 0x76e4aac + 300 · g is 540 + g.
constexpr std::size_t FirstGroupNameText = 540;
constexpr std::size_t GroupTwelveText = 559;
constexpr std::size_t GroupNineteenText = 552;
constexpr std::size_t GroupTwentyText = 1829;

// The load ones (0x645860): 0x230, 0x231 and 0x232.
constexpr std::size_t FullLoadText = 560;
constexpr std::size_t BusyLoadText = 561;
constexpr std::size_t FreeLoadText = 562;
constexpr std::uint8_t BusyLoad = 100;
constexpr std::uint8_t FullLoad = 128;

// 0x447c70: 1 and 15, except in slot 4.
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

ServerGroups GroupServers(std::span<const Protocol::Session::ServerEntry> list)
{
    // By slot, so they come out in the screen's order.
    std::map<int, ServerGroup> bySlot;
    ServerGroups grouped;
    for (const Protocol::Session::ServerEntry& entry : list)
    {
        const std::uint16_t group = GroupOf(entry.code);
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

std::uint16_t GroupOf(std::uint16_t code)
{
    return static_cast<std::uint16_t>(code / CodesPerGroup);
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
} // namespace Client::FrontEnd
