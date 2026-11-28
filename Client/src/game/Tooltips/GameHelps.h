#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The overlay helps (CL8 milestone 6; 06 § HUD y ventanas, "Las ayudas de F1" and "La ayuda del ítem"): modes 1, 2
// and 3 of 0x5b0550, which Hud::DrawTheOverlays chooses with 0x77f3b08.
namespace Client::Tooltips
{

// 0x5b0559..0x5b07ef: 0x620980 and the tooltip lines (HudState::tooltip) from 0, with 0x78abb30 as the count: "\n",
// the title (text 120 or 140, with color 1 and bold), "\n", texts 121 to 139 or 141 to 156 (color 0, no bold) and
// "\n"; the '\n' do not touch their color or their bold. 0x77f3b66 at 1 and 0x5af090(1, 1, count, 0, 2, 1)
// (DrawTheTooltipBox): top left, framed. `mode` is 1 or 2. Without Text.bmd, or with a text of 100 bytes or more
// (Q-C613), it cuts.
[[nodiscard]] Core::Expected<void, std::string> DrawTheFunctionHelp(Ui::GameHudContext& context, std::int32_t mode);

// 0x5b0802..0x5b10c1 (GameItemHelp.cpp): the item help of 0x77f3b0c. 0x620980; with item 0x807 or 0x80f (the arrows
// and the bolts), outside the five type ranges (0..0x9ff, 0xa00..0xbff, 0xc00..0xdff, 0xe00..0x17ff and
// 0x1e00..0x1fff) or negative, 0x77f3b08 at 0 (except in a MuRet intermediate drawing) and nothing else. With ranges 1
// to 4 and a screen width that is not 640, 800, 1024 or 1280 the original divides by 0 and cuts (0x5b097b, Q-C615).
// Otherwise: the per-level table (0x5afc00, FillTheItemHelpLevels); the top box, framed, like the F1 ones ("\n", text
// 160 in blue and bold, the item's name in bold, "\n", " ", "\n", 46 or 51 spaces, 13 " " lines except in the
// scrolls, a green line per set piece, the classes of 0x5af590 and "\n"), with the column width that level 0 has;
// 0x620a00(1), and the columns (the title of 0x5afb00 and the rows of 0x5af9b0) in frameless boxes against the edge,
// from the range's x and the screen's y; 0x620980.
[[nodiscard]] Core::Expected<void, std::string> DrawTheItemHelp(Ui::GameHudContext& context);
} // namespace Client::Tooltips
