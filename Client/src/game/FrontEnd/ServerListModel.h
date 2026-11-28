#pragma once

#include "Protocol/Session/ServerList.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The server list as 0x4c1860 builds it with F4 06 and 0x645860 shows it (CL5.5; 06 § La
// lista de servidores; Q-C62 and Q-C63). Each server goes to group `code / 20`, with number
// `code % 20 + 1`, and the group goes to a screen slot through a table of 25: from slot 4 to 24
// there is a group button, in the order 20, 16, 15, 14, 13, 11, 19, 18, 17, 10, 9, ..., 0 and 12. The
// names and the loads are Text.bmd texts, by index. *Static*, from 0x4c1860, 0x645860 and
// 0x447c70.
//
// What MuRet does differently: the buttons come from every slot with servers, not up to the
// first empty one (Q-C62); the groups without a button (21 to 24) and the codes of 500 or more, which the
// original writes outside its array, are ignored and counted; and a group keeps up to 20
// servers, where the original keeps writing over the next group (Q-C63).
namespace Client::FrontEnd
{
// The groups that fit in 0x4c1860's table: codes 0 to 499.
inline constexpr std::uint16_t ServerGroupCount = 25;
// A group's numbers (code % 20), and the server buttons of MuRet's window (the
// original has 16: Q-C63).
inline constexpr std::size_t ServersPerGroup = 20;
// The slots with a button: 4 to 24, 21 group buttons.
inline constexpr int FirstButtonSlot = 4;
inline constexpr int LastButtonSlot = 24;
inline constexpr std::size_t GroupButtonCount = static_cast<std::size_t>(LastButtonSlot - FirstButtonSlot + 1);

// A group with servers, in its screen slot.
struct ServerGroup
{
    // code / 20.
    std::uint16_t group = 0;
    // 0x4c1860's slot, 4 to 24.
    int slot = 0;
    // In the list's order, up to ServersPerGroup.
    std::vector<Protocol::Session::ServerEntry> servers;
};

struct ServerGroups
{
    // In slot order: button 0's first.
    std::vector<ServerGroup> groups;
    // The servers not shown: of a group without a button, or from the 21st on of a group.
    std::size_t ignored = 0;
};

// 0x4c1860: the group's slot, 23 minus the table of 25, except 12, which goes to 24. Groups 21
// to 23 give 2 to 0 and 24 gives -1, without a button; from 25 on the table is read outside the stack
// (Q-C63) and gives nothing.
[[nodiscard]] std::optional<int> GroupSlot(std::uint16_t group);

// Splits the list into its groups, in the screen's order.
[[nodiscard]] ServerGroups GroupServers(std::span<const Protocol::Session::ServerEntry> list);

// The group name's text: 540 + g, except 12 (559), 19 (552) and 20 (1829).
[[nodiscard]] std::size_t GroupNameText(std::uint16_t group);

// code / 20.
[[nodiscard]] std::uint16_t GroupOf(std::uint16_t code);

// code % 20 + 1.
[[nodiscard]] int ServerNumber(std::uint16_t code);

// 0x447c70: 1 and 15 are non-PvP, except in slot 4.
[[nodiscard]] bool IsNonPvp(int slot, int number);

// The load's text (0x645860): 562 below 100, 561 from 100 to 127 and 560 from 128 on.
[[nodiscard]] std::size_t LoadText(std::uint8_t load);

// The account title (0x641990): 460 non-PvP and 461 otherwise, with the group's name and the
// number.
[[nodiscard]] std::size_t LoginTitleText(int slot, int number);

// The non-PvP mark of 0x645860, from the binary (0x6d6738), not from Text.bmd.
inline constexpr std::string_view NonPvpMark = "(Non-PVP)";

// The server buttons' labels, with the binary's formats: "%s%s %s" for a group's only one
// (0x6d6724) and "%s-%d%s %s" with more (0x6d672c), with the mark or nothing.
[[nodiscard]] std::string SingleServerLabel(std::string_view group, bool nonPvp, std::string_view load);
[[nodiscard]] std::string ServerLabel(std::string_view group, int number, bool nonPvp, std::string_view load);
} // namespace Client::FrontEnd
