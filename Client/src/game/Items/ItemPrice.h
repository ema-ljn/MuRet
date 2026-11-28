#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Kernel/ItemState.h"

#include <cstdint>
#include <optional>
#include <string>

// An item's price (0x54d0f0) and its repair price (0x5b13e0), CL8 milestone 5 (06 § El precio de un ítem). They are
// data functions: they do not open the own record or draw rand(). The repair body (0x5b8150) and the NPC shop price of
// the item body (0x5b2d23, CL9 milestone 6) use them.
namespace Client::Items
{
// The modes they are called with: 0 from the NPC shop grid (0x5b2d2e), 1 from the inventory with the shop open
// (0x5b2e46) and 2 for repairing (0x5b838a). Any other than 0 divides by 3; only 1 discounts the missing durability.
inline constexpr std::int32_t ShopGridPrice = 0;
inline constexpr std::int32_t InventoryPrice = 1;
inline constexpr std::int32_t RepairPriceMode = 2;

// The two pets' levels, the words at 0x723968 + 0x14 · type that 0x421db0 writes with the A9: 0 (the raven) for the
// 0x1a05 and 1 (the horse, WorldRuntime::pets) for the 0x1a04. Only 0x422930 reads them.
struct PetPriceLevels
{
    std::uint16_t raven = 0; // [0x723968]
    std::uint16_t horse = 0; // [0x72397c]
};

// 0x422930: the DWORD product of the level by the species' price, unrounded.
[[nodiscard]] std::uint32_t PetBasePrice(std::uint16_t type, const PetPriceLevels& pets);

// 0x54d0f0(item, mode): the price's low dword, which is what the callers read. Type 0xffff is worth 0. It stops with
// the original's address on: a type outside Item.bmd (0x54d133), an option read outside D+0x2b..D+0x3b, the arrows and
// bolts of level 3 or more (0x54d283, they multiply by the item's address), an x87 operation on an integer of more
// than 53 bits, the 0x1a04 and the 0x1a05 without `pets` (0x54db28) and an infinity in the _ftol of mode 1's
// durability (0x54dc04).
[[nodiscard]] Core::Expected<std::uint32_t, std::string> ItemPrice(const Kernel::ItemState& item,
                                                                   const Tables::ItemTable& items, std::int32_t mode,
                                                                   const std::optional<PetPriceLevels>& pets);

struct RepairPrice
{
    // What it returns in EAX, already rounded.
    std::int32_t gold = 0;
    // What it writes in the caller's buffer, with the thousands separated by commas.
    std::string text;
};

// 0x5b13e0(price, durability, maximum, type, buffer): the repair price, from 0x54d0f0's (capped at 400000000), the
// missing durability and [0x78abb10] (WorldRuntime::field78abb10, 1 with the inventory's repair button), which adds
// 0.05 for each unit of its low byte. It does not read the type.
[[nodiscard]] RepairPrice RepairPriceOf(std::uint32_t price, std::int32_t durability, std::int32_t maxDurability,
                                        std::int32_t field78abb10);
} // namespace Client::Items
