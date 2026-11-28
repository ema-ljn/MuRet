#pragma once

#include <cstdint>
#include <string>

// Zen as the windows write it: 0x5b1130 (the text) and 0x5b10f0 (its color), which the quest dialog (0x41bca1 and
// 0x41bcb6) and the 0x99 text (0x63333d) call. The inventory has the same computation copied inside 0x5f2340
// (0x5f26a7..0x5f2751 and 0x5f2771..0x5f27ef, CL8 milestone 4).
namespace Client::Ui
{
// 0x5b1130(zen, buffer): zen with commas every three digits ("%d,%03d,%03d,%03d" from a billion, 0x6c9844, and so on
// down to "%d", 0x6c9870).
[[nodiscard]] std::string FormatZen(std::uint32_t zen);

// 0x5b10f0(zen): the text color by amount, with unsigned comparisons: from ten million 0xff0000ff, from a million
// 0xff0096ff, from a hundred thousand 0xff18c900 and otherwise 0xff96dcff.
[[nodiscard]] std::uint32_t ZenColor(std::uint32_t zen);
} // namespace Client::Ui
