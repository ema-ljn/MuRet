#pragma once

#include "Core/Expected.h"

#include <string>

// The pets' tooltip, 0x422990 (CL9 milestone 4; 06 § El A9 de las mascotas, "El tooltip de las mascotas"): what
// 0x5b2aa0 (Tooltips/ItemTooltips.h) shows of the Dark Horse (0x1a04) and the Dark Raven (0x1a05), with what the A9
// writes at 0x723960 (WorldRuntime::pets).
namespace Client::Tooltips
{
struct ItemTooltipContext;

// 0x422990(x, y, item, grid), from 0x5b2b0d with the lines already clear. With another type it returns false and does
// nothing. With a pet it returns true and 0x5b2aa0 exits (0x5b2b17):
//   - if the item is not the last request's (WorldRuntime::petTooltipQuery: the box's place, the type and D+4), it
//     notes it and requests its A9 (C1 06 A9, the pet, 0x78abb28 and 0x78abb2c) through GameHudContext::send, without
//     drawing anything; an intermediate drawing neither notes nor requests;
//   - if it is the same, 0x4221d0 with the pet, the price (NPC or personal shop, with its queries and the format of
//     0x5b1130), the name, the experience, the level, the raven's damage and speed, the durability, the horse's
//     required level and the raven's required leadership (each with the own record opened and closed), the classes,
//     the options (0x4225d0 and 0x5b2320 for each one) and, on the horse, the defense and the skill; then, the box
//     (0x5af090) above (x, y) with the height its whole lines and half lines count, which are not those of 0x78abb30
//     and 0x78abb34.
// What it writes into the world (the pets table, the horse's option on the item, the noted request) goes to copies in
// an intermediate drawing.
[[nodiscard]] Core::Expected<bool, std::string> DrawThePetTooltip(const ItemTooltipContext& context);
} // namespace Client::Tooltips
