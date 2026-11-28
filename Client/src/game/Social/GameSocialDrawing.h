#pragma once

#include "Core/Expected.h"

#include <string>

// The duel drawings (CL9 milestone 7, phase 2; 04 § El duelo): the score, the HUD's 14th child (0x5a6cc0), and the
// request box, from the scene's 2D pass (0x5f37f0, at 0x637683). In 640 × 480, with the GL state they leave and the
// texts' globals (0x6c8e5c, 0x6c8e64 and the DC's font) in HudState.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

namespace Client::Social
{

// 0x5a6cc0: with the duel on (0x78abad8), half the world's width (0x5bb8d0, with its queries) as center; the black
// band at 0.5, 300 × 40 from (center − 150, 20) with the 1-wide frame in (0.65, 0.53, 0.34); in the big bold font, the
// hero's name (black shadow at center − 79, 0x1f and in 0xffff9600 at center − 80, 0x1e), "vs" in 0xff0096ff at the
// center and the rival's name (shadow at center + 81 and in 0xff1919ff at center + 80); in the normal font, the own
// score (0x78abaf8, in 0xffff9600) and the rival's (0x78abafc, in 0xff1919ff) at center ∓ 80, 0x30. Each text's x
// calls 0x5bb8d0 again: eight in all, each with its rand(). Leaves the normal font, the background at 0, the text in
// 0xff1919ff and blending off.
void DrawTheDuelScore(Hud::GameHudContext& context, Ui::PanelRegistry& panels);

// 0x5f37f0: with the duel request (0x78abad9), the box of Message_box1 and 2 (0xfb and 0xfc) at (213, 110), 213 wide,
// the duel image (DuelWindow.jpg, 0x650) added at (256, 120), the yes buttons (0xf1, 0xf2 with the mouse in
// 0xed..0x133 × 0xfa..0x10f) and no (0xf3, 0xf4 in 0x14c..0x192), "[name]" in the big bold font (black shadow at
// (0x140, 0x8d) and in 0xff18cfff at (0x13f, 0x8c)) and texts 910 and 911 in bold (shadow at 0x140 and in 0xffdcdcdc
// at 0x13f). No rand(). Leaves bold, the background at 0, the text in 0xffdcdcdc and blending off. Without Text.bmd,
// it fails.
[[nodiscard]] Core::Expected<void, std::string> DrawTheDuelRequest(Hud::GameHudContext& context);
} // namespace Client::Social
