#pragma once

#include "Core/Expected.h"

#include <string>

// The guild drawings (CL9 milestone 8, phase 2; 06 § El gremio en pantalla): the war score, the HUD's 2nd child
// (0x5a7640), and the panels' dialog (0x660830), the last of 0x6579e0. In 640 × 480, with the GL state it leaves and
// the text globals (0x6c8e5c, 0x6c8e64 and the DC's font) in HudState.
namespace Client::Ui
{
class PanelRegistry;
struct GameHudContext;
struct PanelDialogState;
} // namespace Client::Ui

namespace Client::Guild
{

// 0x5a7640: always 0x620980 (no blending) and the text background at 0x80000000. With the war (0x576796c) and the
// hero in a guild (+0x1f2 other than −1): normal blending, x = world width (0x5bb8d0, once, with its queries) · 0.5 −
// 20 in float and bold (0x5763558); the text color 0xff003cff (0xffff9600 with the team, 0x576796e); the hero's guild
// mark (0x5f16a0 with the clear background), 8 × 8 at (x, 4), its score (0x57678b4, "%d") at (_ftol(x) + 13, 4) and
// its guild's name (+0xd of the entry) at (_ftol(x) + 30, 4); the other color, the rival's mark (the first entry from
// 0 to 1998 with its name, 0x5767804; with none, 0) at (x, 19), its score (0x57678b8) at (_ftol(x) + 13, 19) and its
// name (0x5542f0) at (_ftol(x) + 30, 19). An empty text is not drawn, but it is measured (0x77f31b8). The second score
// (0x576796f, F3 23 of 0x4d9f00) runs after, independent of the war and the hero's guild; it looks up both current
// names in 0..1998 and uses the same score counters.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWarScore(Ui::GameHudContext& context,
                                                                Ui::PanelRegistry& panels);

// 0x5a7eb0, after the HUD even when hidden (0x637668): the event clock and the F3 24 outcome.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventTimeAndOutcome(Ui::GameHudContext& context);

// 0x660830, open: with 0x6602b0's own drawing (+0xc), only that (DrawTheGuildPositions). Otherwise, 0x6576a0's box
// (0x620c10, glColor4f(1, 1, 1, 0.8), Message_box2 (0xfc) of 213 × 5 at (214, y), a row of Message_box1 (0xfb) of
// 213 × 40 for every 40 of (height − 10), the rest scaled and 0xfc again; then white and 0x620980), with y = 0x82 −
// height / 2 (never less than 40) or 10 with +0xa24. Then the normal font (0x5763554), normal blending and colors −1
// and 0; each row at (0x140 − width / 2, y + 0x14 + 15 · i) with 0x5542f0, which neither draws an empty one nor
// measures; and the buttons (0x45a9d0 and 0x45b2e0) at y + 9: Accept at 0x127, or Yes and No, or Confirm and Cancel
// at 0x104 and 0x14a. The text field (0x10) and the clock (8) stop: no MuRet dialog opens them (P-C285). No rand().
[[nodiscard]] Core::Expected<void, std::string> DrawThePanelDialog(Ui::GameHudContext& context,
                                                                   Ui::PanelDialogState& dialog);

// 0x64f1c0, the drawing of the positions window (+0xc of the panels' dialog, instead of everything): 0x6576a0's box,
// 0x96 high, with its corner (0xd6, 0x37) in 0x7d536d0 and 0x7d536d4 for the step; 0x620a00(1); 0x471520's two
// 170 × 20 boxes at (x + 0x14, y + 0x13) and (x + 0x14, y + 0x27), the one of 0x7d536d8's position (0x40 or 0x20)
// filled; texts 1311 (y + 0x18), 1312 (y + 0x2e), 1314 with the name from 0x7d535a8 and 1301 or 1302 (y + 0x4b) and
// 1315 (y + 0x5d), centered in _ftol(rx · 170) from x + 0x14 (0x5542f0 in mode 2), with whatever font and colors are
// set; and the buttons 0x7d535b8 at (x + 0x37, y + 0x75) and 0x7d53640 at (x + 0x6e, y + 0x75). No rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheGuildPositions(Ui::GameHudContext& context);
} // namespace Client::Guild
