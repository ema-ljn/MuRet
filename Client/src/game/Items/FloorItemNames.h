#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct FloorItemState;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameHudContext;
} // namespace Client::Hud

// A floor item's name (CL9 milestone 5; 06 § Los ítems del piso): 0x5ba790, called by the hover with the picked one
// (0x5bbd62) and by Alt with the others (0x5bbdd2). *Static*, read in the ASM.
namespace Client::Items
{

// 0x5ba790(index, object, +8, +0x1f, +0x20, atTheItem): the normal font and the colors (0xffffffff, 0xff000000); the
// text and its color by the model (the object's +2, the type + 0x212 or the one 0x608350 gave it by level): zen, the
// jewels, the boxes, the fireworks, the fruits, 0x1a1d (Skill.bmd's skill level + 30, with its access) and the others
// with the color of the level and the options, the set's name (0x40cf30, which switches to white, bold and 0xff00ff00
// on 0xffc83c3c), " +level" and the skill, option and luck texts. It draws it with 0x554180 centered (half the width,
// signed, times 640 and by W unsigned) and 15 above the mouse or, with `atTheItem`, above the item's projection (+0x5c
// and +0x5e). It ends with the colors at (0xffc8e6ff, 0xff000000). Two cases stop: 0x1a0a, which reads the monsters
// (0x7681eb8, CL9 milestone 2), and the models and levels with which the original does not write the text and draws
// what was left on its stack (07 Q-C1242).
[[nodiscard]] Core::Expected<void, std::string>
DrawTheFloorItemName(Hud::GameHudContext& context, const Kernel::FloorItemState& item, bool atTheItem);
} // namespace Client::Items
