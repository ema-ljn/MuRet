#pragma once

#include <cstddef>
#include <cstdint>

namespace Client::Maps
{
// 0x5f0b40(map): the Text.bmd line with the map's name, returned as an address (0x76bd1dc + index · 300). It compares
// the signed int in this order: 0x22 line 1851, 0x21 line 1850, 0x1e 669, 0x1f 59, 0x12 to 0x17 (Chaos Castle) 57,
// 0x18 to 0x1d (Kalima) and 0x24 58, 0x0b to 0x11 (Blood Castle) 56, 0x0a 55, 0x20 39, and any other 30 + map
// (0x76bf504 + map · 300). Used by the party window (0x5f0ec8) and the F3 03 welcome line (0x4c4b38).
[[nodiscard]] std::size_t MapNameText(std::int32_t map);
} // namespace Client::Maps
