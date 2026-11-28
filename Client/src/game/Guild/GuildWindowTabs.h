#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

// The draws of the guild window's three tabs, [0x7d536f0] (CL9 milestone 8, phase 5; 06 § La ventana del gremio):
// 0x64fdf0 (tab 0, the guild and its notice), 0x6507d0 (the members) and 0x651400 (the union), with the lists on
// Ui::OldList and their template draw (0x47a810 and its equivalents 0x480520 and 0x481bf0).
namespace Client::Guild
{

// 0x652291..0x6522c0: the draw of tab `tab` after the tabs (0x651f1c..0x652275), with the bold in white on 0 they
// left.
//
// 0x64fdf0, tab 0: the 53 frame at (x + 0x45, y + 0x68) with the 48 mark of the hero's guild (0x5f16a0(+0x1f2, 1));
// the 140 × 62 frame at (x + 0x19, y + 0xaa) and at x + 0x1e, every 0xd from y + 0xb2, each row in a box of
// _ftol(rx · 140) (0x5542f0 in mode 1): "%s :" with 1332, 1256 with the score (0x78aba3c), the members (0x78aba38:
// the master, with the own record open (0x57630c8, its closing draws the rand()), 1362 with class 4's limit,
// leadership / 10 + level / 10 up to 0x50, or level / 10 in the others; the rest, 1310) and "%s : %s" with 1321 and
// the name at +0x88 (or 1361); 1323 at y + 0xfa in _ftol(rx · 136); the notice list, 0x88 × 0x50, hanging from
// y + 0x154 at x + 0x1c; the 140 × 95 frame at (x + 0x19, y + 0xf6), and the +0x42c button with 188 (the master) or
// 189.
//
// 0x6507d0, the members one: with 0x78aba38 above 0, list +0x94 (0x47a810 with 0x46cb20 and 0x46d050); with the hero
// as master, the three buttons.
//
// 0x651400, the union one: without a union (+4 of the hero's entry, without checking −1), texts 1257 to 1267 at
// x + 0x19 from y + 0x6a (0x554180), the last six in white on 0xff0000cc; with a union, list +0x2f8, 0xa0 × 0x50, at
// x + 0xf, hanging from y + 0x6e plus the notice list's height (+0x224, not its own: Q-C1293) and, with the hero as
// master, +0x64c with 1324 and +0x6d4 visible if it is the union master, or +0x64c with 1325 and +0x6d4 off (6).
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildTab(Hud::GameHudContext& context, std::int32_t tab);
} // namespace Client::Guild
