#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The party window (P): its step (0x5e3c80) and its drawing (0x5f0be0), empty since CL8 (milestone 6, 08 P-C92) and
// with members since CL9 (milestone 7: the list arrives with the C1 42, 0x4d67e0), and the party bars over the heads
// (0x5a89b0). Contract: 06 § Lo social en pantalla.
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Ui
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Ui

namespace Client::Party
{

// 0x5e3c80, from 0x5ee140 (0x5ee18d): with the party open, the mouse over the window (from the corner the drawing
// left, 0xbe × 0x1b1) belongs to the UI (0x775857c), and the click on the close button (0x19, 0x18b, 0x18 × 0x18) is
// spent, sets 0x77f3b10 to 0 and 0x6c8dcc to 6 and closes the party with Close(4), which plays a sound. No rand().
[[nodiscard]] Core::Expected<void, std::string> StepThePartyWindow(const Ui::GameUiContext& context,
                                                                   Ui::PanelRegistry& panels);

// 0x5f0be0(x, y), the first thing in 0x5f7440, which calls it with (0x1c2, 0). With the party closed, the world's
// width (0x5bb8d0, which takes rand() with the inventory open), colors −1 and 0 and, with members, the right-hand list
// (0x5f130a): for each one, from (width − 50, 5) every 19, the 50 × 19 background (lighter with the mouse; none with
// +0x1c at −1), the 43 × 4 bar and its tenths (+0x18, up to 10), red on top if it is not in the viewport or green with
// +0x20 & 8, and the name in the normal font; the edges (Message_box2) and the sides. Open, the corner, the background
// without blending, the title (text 190) in bold, the empty party's seven lines (191 to 197) or, per member every 0x23,
// the name in bold (the first, the leader, on a red background), the map (0x5f0b40) and the cell, the life bar (the
// lesser of +0x10 and +0x14 over +0x14, of 90) with its numbers and, if the hero is the leader or that member, the kick
// button with its help (189); at the end, the close button with its own (221). Leaves normal blending. No rand() other
// than the width's.
[[nodiscard]] Core::Expected<void, std::string>
DrawThePartyWindow(Ui::GameHudContext& context, Ui::PanelRegistry& panels, std::int32_t x, std::int32_t y);

// 0x5a89b0, the HUD's child 3: with members (0x78aba48 at 1 or more), each one with a character (+0x1c at 0 or more)
// has its bar over the head (+0x138 + z + 100, projected with 0x6205b0), 19 to the left: with the mouse over it,
// "HP : %d0%" 6 higher; the shadow (0, 0, 0, 0.5) with normal blending, and added the 42 × 5 border, the 38 × 1
// background and the 3 × 2 tenths (+0x18, up to 10). It ends opaque and in white. No rand(). Without the world pass's
// projection, it fails.
[[nodiscard]] Core::Expected<void, std::string> DrawThePartyLife(Ui::GameHudContext& context);
} // namespace Client::Party
