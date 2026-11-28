#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The character window in the game HUD (06 § La ventana del personaje, CL8 milestone 5): 0x5ee2e0(0x1c2, 0) and what
// 0x5f7440 draws after it, the close button 0x118 with the help of text 222. In 640 × 480, with the GL state it leaves
// and with the protection table's rand() in their order: 3848 for each close of the own record that leaves it with no
// accesses (between 12 and 18 per frame, by equipment and class) and 60 for that of the skill's Skill.bmd entry
// (0x5513d0, classes 0 and 3).
//
// The texts go through ITextDrawer::DrawColumns, with the box of the width 0x554180 gives them (W · k / 640): the
// background fills it whole, and a text that does not fit is clipped as in 0x467060 (mode 1 shows the end and mode 2
// the center).
namespace Client::Hud
{

// 0x555a70(pk): the name's color (0x6c8e5c) by the hero's +0x375.
[[nodiscard]] std::uint32_t PkNameColor(std::uint8_t pk);

// 0x5ee2e0(0x1c2, 0) and 0x5f7440's button (0x5f74a3..0x5f759a), with the character window open. It stops with its
// address without the chosen server (`chosenServer`, 0x5ee629), without Text.bmd, Item.bmd or Skill.bmd where it reads
// them, with the record's name without its 0 in its 11 bytes and with +0x46 at 0 and +0x44 above 10 (0x5f08ac's
// division, an exception in the original). The hero's skill (+0x6c + [+0x425]) is always within the record's 256
// bytes: the 0x5f04cd stop is not reached (P-C122).
[[nodiscard]] Core::Expected<void, std::string> DrawTheCharacterWindow(Ui::GameHudContext& context);
} // namespace Client::Hud

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui
