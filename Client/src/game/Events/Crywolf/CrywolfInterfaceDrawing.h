#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The Crywolf interface (CL9 milestone 11, phase 3; 06 § La interfaz de Crywolf): 0x4365c0, HUD child 21 (0x5a74d4),
// *static*. GS 0.97 has no Crywolf: it is filled by BD 00 and BD 02 to 09 (Events/EventPackets), with the client's ASM
// as oracle; what the VM has to confirm is in 08 (P-C298).
namespace Client::Events::Crywolf
{

// 0x4365c0: 0x620a00(1) and, in this order,
//   - with 0x723bf9 at 1, the results: frame 0x6e2, titles 680, 681, 1973 and 1977 and the five rows (place, name,
//     class and points, without those with points at −1), text 2000 and 2001 (or 2009 and 2010) and button
//     0x6c9..0x6cb, which with the mouse over it and 0x723bdc at 5 closes the window;
//   - with the notice (0x6c73d1) at 0 or more, banner 0x6b6 + 0x6c73dc entering, staying and leaving, the rank with its
//     letter and the nine digits of the experience;
//   - with the stage at 3 or 4 in map 34, the state: the frame, the five altars, the Balgass bar, the clock
//     (0x6218f0), the light and the tips of 0x430180 (with stage 3); ends with 0x620980.
// Without the state it does not return to 0x620980 and the blend stays at normal. Writes into
// `context.crywolfInterface` (or WorldRuntime's): the banner, bar, clock and tip counts. Stops only without a Text.bmd
// text or with one that does not fit the 300-byte stack buffer.
[[nodiscard]] Core::Expected<void, std::string> DrawTheCrywolfInterface(Ui::GameHudContext& context);

// 0x436240, HUD child 26 (0x5a74ed): with the stage at 3 or 4 (0x435a00) in map 34 (0x4303c0) it leaves the text in
// 0xff1594ff over 0 for the rest of the HUD, and with 0x723a44 at 1 or 2 draws the altar contract window
// (0x436274..0x43656f, phase 4): background 0x6cc, the No and the Yes (with 1) or the OK (with 2), which with the mouse
// over them and their number in 0x723bdc (1, 2 or 3) close the window, and the two lines of 0x723a48 with 0x623690.
// 0x435b00 opens it (Events/Crywolf/CrywolfContract). Writes into `context.crywolfInterface` (or WorldRuntime's). No
// rand().
[[nodiscard]] Core::Expected<void, std::string> DrawTheCrywolfContract(Ui::GameHudContext& context);
} // namespace Client::Events::Crywolf
