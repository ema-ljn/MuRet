#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL9 milestone 11, phase 2: the event NPC windows (06 § Las ventanas de los NPC de evento), Devil Square's (panel
// 0x1000) and Blood Castle's (0x4000), with the corner at (0x104, 0) to the right of the inventory. The drawing is
// *static*, with no GL trace (what the VM has to confirm is in 08, P-C294).
namespace Client::Ui
{
class PanelRegistry;
}

namespace Client::Kernel
{
struct OwnInventory;
} // namespace Client::Kernel

namespace Client::Hud
{
struct GameHudContext;
struct GameUiContext;
} // namespace Client::Hud

namespace Client::Events
{

// 0x5f58f0, from 0x5f7440 unconditionally: with 0x1000, the background (0x5ee220), the six buttons (0xf0 of 140 × 35
// from (x + 25, 187), in steps of 40) with 645 or 646 and the levels of table 0x6ba4c8 (0x6ba4f8 with class 3 or 4 of
// the own record, which opens and closes on each row), title 39 and texts 670 to 675. With 0x4000, Blood Castle's seven
// (140 × 30 from 170, in steps of 35) with 847 or 848 and table 0x6ba528 (seven more with the hero's class 3 or 4),
// those not of the own record's level in gray, title 846 and 832 in 26-byte lines. A MuRet drawing that projects does
// not write the corner or the Devil Square levels.
[[nodiscard]] Core::Expected<void, std::string> DrawTheEventNpcWindow(Hud::GameHudContext& context,
                                                                      Ui::PanelRegistry& panels);

// 0x5e5920, one of the inventory's siblings (0x5ee1a3): with the click on a square, the level against those the
// drawing left (686 or 687 and the inventory closed) or invitation 0x1c13 of level 0 or the square's (677 without it)
// and C1 05 90; on a bridge of its level, the bridge's cloak 0x1a12 (7 also accepts the level 6 one; 854 without it)
// and C1 05 9A. Both with XOR32, in the clear.
[[nodiscard]] Core::Expected<void, std::string> StepTheEventNpcWindow(const Hud::GameUiContext& context,
                                                                      Ui::PanelRegistry& panels);

// 0x5583a0(type, level): the inventory slot (0 to 63) of an item of the type with +0x3c above 0 and the level ((+4 >>
// 3) & 0xf), or any with −1, walking from column 7 to 0 and in each from row 7 to 0; −1 if none.
[[nodiscard]] std::int32_t FindInventoryItem(const Kernel::OwnInventory& inventory, std::uint16_t type,
                                             std::int32_t level);

// 0x558740(type, level): how many inventory slots have the type (the signed word), +0x3c above 0 except with type −1,
// and the level, or any with −1. With (−1, −1) it counts the free ones.
[[nodiscard]] std::int32_t CountInventoryItems(const Kernel::OwnInventory& inventory, std::int32_t type,
                                               std::int32_t level);
} // namespace Client::Events
