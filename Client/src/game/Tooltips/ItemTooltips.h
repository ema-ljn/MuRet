#pragma once

#include "Core/Expected.h"
#include "Kernel/ItemState.h"

#include <cstdint>
#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The bodies of an item's tooltips (06 § Los tooltips, CL8 milestone 5): the item's (0x5b2aa0) and the repair one
// (0x5b8150); the skill's (0x5b90b0) is in SkillTooltip.h. Each one clears its lines of HudState::tooltip, fills them
// and draws the box (0x5af090). 0x5f7bc0 calls them with the own record open around them.
namespace Client::Tooltips
{

struct ItemTooltipContext
{
    Ui::GameHudContext& hud;
    // 0x78a9bac and 0x78a9ba8: where it goes (the top center of the box).
    std::int32_t x = 0;
    std::int32_t y = 0;
    // The item of 0x78abaa8: the cell or the equipment slot. The pets' tooltip writes the horse's option into it
    // (0x4225d0, from 0x423b07); an intermediate drawing passes a copy.
    Kernel::ItemState& item;
    // 0x78aba27: the last argument of 0x5c4110, or 0 from the equipment.
    std::uint8_t grid = 0;
};

// 0x5b2aa0(x, y, item, grid): with 0x1a04 and 0x1a05, the pets' one (0x422990, Tooltips/PetTooltip.h) and nothing
// else.
[[nodiscard]] Core::Expected<void, std::string> DrawTheItemTooltip(const ItemTooltipContext& context);

// 0x5b8150(x, y, item, grid): with the repair mode, the name, the durability and the repair price.
[[nodiscard]] Core::Expected<void, std::string> DrawTheRepairTooltip(const ItemTooltipContext& context);
} // namespace Client::Tooltips
