#pragma once

#include "Core/Expected.h"

#include <string>

// The chaos box drawing (0x5febf0, CL9 milestone 6; 06 § La caja del caos), which 0x5f7440 always calls after the
// shop: the window at (0x104, 0), the 8 × 4 grid, the mode's title (0x7d3a114) with the tax (0x78aba34), and with the
// mix not requested (0x7d2dc80 below 2) the name, the chance and the price of the mix that 0x5fd4f0 left
// (ChaosMix/ChaosMix.h), the chaos jewel's warning, each type's warnings and the mix button with its help. Writes the
// price (0x7d2dca0) that the next step's mix button reads. Draws no rand().
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

namespace Client::ChaosMix
{

// 0x5febf0: without the box open (IsOpen(0x100), which draws no rand()), nothing. Open, it sets the NPC window's
// corner (0x78aba28 and 0x78aba2c) to (0x104, 0) and draws. Leaves the bold font and the last line's colors, or, with
// the mouse over the mix button, the normal font and the white on transparent of its help (0x554340). A missing
// Text.bmd text, or one that overflows its stack buffer, cuts.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChaosBox(Ui::GameHudContext& context, Ui::PanelRegistry& panels);
} // namespace Client::ChaosMix
