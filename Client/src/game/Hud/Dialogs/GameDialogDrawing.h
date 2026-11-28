#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

// The game dialogs in the drawing (06 § HUD y ventanas, "Los diálogos del juego", CL8 milestone 6): 0x62db60, which
// the 2D pass calls in scene 5 (0x637696) between the duel request and the tooltips. It draws the open dialog
// (0x7d3a10c) with its frame and its texts in 640 × 480, with the GL state its primitives leave.
namespace Client::Hud::Dialogs
{

// 0x62db60 always leaves the blend off (0x620980), the color white, the text colors at (−1, 0) and the normal font in
// the DC, and ends with the normal blend (0x620a00(1), 0x63140b). With the dialog open:
//   - 0x6e, the Esc menu: the 5 buttons (0xf0 at (260, 60 + 30i, 120, 22)), the hovered one again in additive with
//     (0.8; 0.6; 0.4), and the texts 381, 382, 383, 385 and 384 centered;
//   - 0x93, the options: the 7 rows, the texts 385, "%s  On"/"%s  Off" with 386, 387 and 919, 389 with the volume bar
//     (its corner stays in 0x7d3a120 and 0x7d3a124 for the step), the glow frame and its 5 boxes with their digits,
//     and the texts 1840 and 388;
//   - the boxes (0x8a, 0x8c, 0x8b, 0x97 and 0x9b): the frame (0xfc, 4 or 7 rows of 0xfb and 0xfc), the buttons with a
//     state (0xf0 + state), 0x8c's arrows (which spend the click, Q-C553) and the lines of 0x8a and 0x8c centered;
//     those of 0x8b, 0x97 and 0x9b are drawn by the event object ([0x723c60] + 0x10, CL9.5): without it, nothing, and
//     with it, it stops;
//   - the generic OK dialogs with text (0x70, 0x6f, 0x1a, 0x1c, 0x87, 0x88 and 0x8f): the 0xf0 frame, the OK (0xf1,
//     or 0xf2 hovered) and their Text.bmd texts;
//   - the party invitation (0x77) and the trade request (0x78) (CL9 milestone 7, phase 2): the 0xf0 frame, the yes
//     (0xf1 or 0xf2) and the no (0xf3 or 0xf4), the text 418 with the name (that of the first live one with the key of
//     0x78aba4c, 0x5110d0, or the trade partner's, 0x78aba1c, in the big bold font) and the question (425 or 419);
//   - the confirmation box 0x9c (0x471ae0, CL9 milestone 7, phase 6a): the frame (0xfc, 0xfb as high as the lines and
//     0xfc), the yes (0xf1 or 0xf2) and the no (0xf3 or 0xf4), or only the yes, by kind, and the lines centered,
//     without blend;
//   - the money one (0x73): the 0xf0 frame, the EDIT of [0x5763504] set up every frame (filter 9, limit 8, 0x46 × 0xe
//     at (0x11b, 0x5f)) with the focus and its drawing, and the text 420, 421 or 422 by 0x78abacc;
//   - the personal shop's title or price one (0x91, 0x6304f2): the frame (0xfc, 2 rows of 0xfb and 0xfc from y 100),
//     the EDIT with the focus and its drawing, without blend, the OK (0xf1 or 0xf2) at (255, 160) and the cancel (0xf3
//     or 0xf4) at (330, 160), 52 × 16, and the text 1128 or 1129 by mode;
//   - the yes/no dialogs (0x94, 0x96, 0x98, 0x99, 0x9a, 0xa3, 0xa8, 0xa9 and 0xaa, CL9 milestones 6 and 7; 0x94 enters
//     through 0x630cbd): the frame at y = 100 (210 for 0xa3; 0xfc, 3 rows of 0xfb, 4 for 0x9a, and 0xfc), the buttons
//     of 0x7d368b8 with state 1 to 4 at (x + 213, y + 100) (0x9a's yes in red without the item; 0xa8's three in 0x646
//     with their texts) and the lines in the bold font, with the x, the y and the colors that 0x78abb14's notice says
//     (0x632637), with the mix title first if it is 2; or 0xa9's in yellow (0x632a45); or 0xaa's centered like the
//     boxes' (0x632472);
//   - the chaos box mode (0x8e) and the jewel mix (0xa1, 0xa2, 0xa4, 0xa5 and 0xa6, CL9 milestone 6): the frame from
//     y = 120, the 0xf0 buttons white when hovered, the chosen jewel in (1; 1; 0.2), the Text.bmd texts and the tail's
//     title (734, 1801 or 1800) in bold with 0xff0080ff; with a chosen jewel, 0xa4 draws 0x6e5660's list (0x62f739,
//     ChaosMix/JewelBundleList) and its OK and its cancel.
// The rest stop with the wave that brings them, like their step. In a MuRet intermediate drawing, the bar's corner and
// the click are not written. Without Text.bmd, or without one of its texts, it stops.
[[nodiscard]] Core::Expected<void, std::string> DrawTheGameDialogs(GameHudContext& context);
} // namespace Client::Hud::Dialogs
