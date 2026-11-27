#pragma once

#include "Player/ItemSetOptionList.h"

#include <array>
#include <cstddef>
#include <cstdint>

// What 0x40e1b0, 0x40d010, 0x40da20, F1, the chat's help and the tooltips write in the sets' object, [0x6eed3c] (the
// static of 0x6e5930), from +0x9304 to +0x940b. A typed projection, not the layout; each field says its offset. The
// tables at +4 and +0x8004 are Tables::ItemSetTables; +0x9304 and +0x9305 are the tooltips' (CL8 milestone 5) and
// nothing ported reads +0x9306. It starts at zero, like the static object, which its constructor (0x40f2a0) also
// zeroes from +0x9304 to +0x9347. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los stats del registro
// propio".
namespace Client::Player
{
// 0x40d010's two piece kinds, the item's byte +0x1c & 3 minus 1: the names of +0x9308 and +0x9328, and in the list the
// counts of +0x9348 and +0x9349 and the sets of +0x934a and +0x934b. With & 3 at 3 it falls outside (Q-C187).
inline constexpr std::size_t ItemSetKindCount = 2;
// What each name takes: ItemSetOption.bmd's with its 0.
inline constexpr std::size_t ItemSetNameSize = 0x20;

struct ItemSetState
{
    // +0x9304: the inventory's set window. The mouse over the label turns it on (0x40e6c0, in the step) and its drawing
    // turns it off (0x40e850, from the tooltips), which shows it with 1.
    std::uint8_t byte9304 = 0;
    // +0x9305: the label of an ItemSetOption.bmd set, its index + 1. The "/" command and the name (0x40ebd0) write it
    // and 0x40eca0 draws it until F1 (0x5aca34), 0x5ae360, opening the character (0x65c97b) or an unused set turns it
    // off.
    std::uint8_t byte9305 = 0;
    // +0x9306: opening the character zeroes it (0x65c97b).
    std::uint8_t byte9306 = 0;
    // +0x9307: 0x40d010 zeroes it, sets it to pieces − 1 when a set does not fit its kind's name, and with a complete
    // set adds 1 per option if it is not 0.
    std::uint8_t byte9307 = 0;
    // +0x9308 and +0x9328: each kind's set name, copied with its 0 (strcpy).
    std::array<std::array<char, ItemSetNameSize>, ItemSetKindCount> names{};
    // +0x9348..+0x940b: the counts, the sets and the option list that 0x40da20 and 0x550d10's helpers read. 0x550d10
    // zeroes its marks after 0x40e1b0 (0x550d29).
    ItemSetOptionList list;
};
} // namespace Client::Player
