#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Items/ItemPrice.h"
#include "Kernel/ItemState.h"

#include <cstdint>
#include <span>
#include <string>

// The chaos box (CL9 milestone 6, phase 3; 06 § La caja del caos): what it is worth (0x5fd120), the mix kind
// (0x5fd4f0 and the mode of 0x5fd4b0), the inventory room the mix asks for (0x5fd200) and the chance and price its
// drawing computes (0x5febf0). Called by 0x5d8d50 with the box open (0x5d8db6..0x5d8dda), the mix button (0x5fe690)
// and the drawing. The globals stay in NpcWindowState.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Npc
{
struct NpcWindowState;
} // namespace Client::Npc

namespace Client::ChaosMix
{

// 0x5fd120(grid, columns, rows): the sum of what each item with a type and +0x3c is worth (0x54d0f0 in mode 0, with
// the jewels' fixed prices: 100000 for Bless 0x1c0d, 70000 for Soul 0x1c0e, 40000 for Chaos 0x180f and 450000 for
// Life 0x1c10 and Creation 0x1c16), in the int that wraps around. Without a chaos jewel, 0. `pets` are the pet levels
// that 0x54d0f0 reads with the 0x1a04 and the 0x1a05.
[[nodiscard]] Core::Expected<std::int32_t, std::string> ChaosBoxValue(std::span<const Kernel::ItemState> grid,
                                                                      const Tables::ItemTable* items,
                                                                      const Items::PetPriceLevels& pets);

// 0x5fd4f0(0x7d2d380, 8, 4): counts what is in the box and returns the mix kind (0 with none; negative the error that
// the mix button warns about). Leaves in `npc` the largest widths and heights (0x6c9da0, 0x6c9da4), the excellents'
// value (0x7d2dc88), luck (0x7d2dc9c), the wings' option (0x7d2dc90) and, by kind, the level (0x7d2dc8c), the cape
// (0x7d2dc84) and the wings' chance (0x7d2dc94). The second-level wings (and the 0x18 cape) open the own record to
// read the level (+0xe): 3848 rand() on closing it without other accesses.
[[nodiscard]] Core::Expected<std::int32_t, std::string> ChaosMixKind(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                     const Tables::ItemTable* items);

// 0x5fd4b0: the kind of 0x5fd4f0 filtered by the box mode (0x7d3a114): in mode 0, mix 1 (the chaos weapon) gives 0;
// in mode 1, only mix 1; in the others, any.
[[nodiscard]] std::int32_t ChaosMixKindForMode(std::int32_t kind, std::int32_t mode);

// 0x5d8db6..0x5d8dda: with the box open, in each step of 0x5d8d50, what the box is worth (0x7d2d378) and the mix kind
// (0x7d2dc98).
[[nodiscard]] Core::Expected<void, std::string> ScanTheChaosBox(Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                const Tables::ItemTable* items);

// 0x5fd200(grid, columns, rows, width, height): whether there is a width × height place with every cell typeless
// (0xffff). With the height at 0 or less, yes.
[[nodiscard]] bool HasRoomFor(std::span<const Kernel::ItemState> grid, std::int32_t columns, std::int32_t rows,
                              std::int32_t width, std::int32_t height);

// The room the mix button asks for for kind `kind` (0x5fe9bd..0x5fea04): 5 × 4 for the chaos weapon, the wings and the
// 0x0b's cape; 2 × 2, 1 × 1 or 3 × 3 for others; the box's largest widths and heights (0x6c9da0 and 0x6c9da4) for the
// rest.
struct MixRoom
{
    std::int32_t width = 0;
    std::int32_t height = 0;
};
[[nodiscard]] MixRoom MixRoomFor(const Npc::NpcWindowState& npc, std::int32_t kind);

// What the mix button does with kind `kind` (0x5fe9a2..0x5fe9b6, the table at 0x5feb88).
enum class MixButtonAction
{
    LevelTooLow,  // −0x18: text 1171 with 10.
    NotTheSame,   // −8 and −2: text 600.
    NothingToMix, // 0: 580 with 591.
    Mix,          // those that mix: the room and dialog 0x98.
    Nothing,      // the others: only the sound.
};
[[nodiscard]] MixButtonAction MixButtonActionFor(std::int32_t kind);

// 0x5fedac..0x5ff0c2: the chance (ESI) and the price (0x7d2dca0) that the drawing computes from the mix kind, with the
// price text of 0x5b1130. Without a case of its own, the chance is what the box is worth / 20000 (capped at 100) and
// the price the chance · 10000.
struct MixQuote
{
    std::int32_t rate = 0;
    std::uint32_t price = 0;
    std::string priceText;
};
// The live DWORDs after the format at 0x6c9dc0 that the 2's rate also reaches (Q-C942). The defaults are those of
// .data; the drawing lends the current world's globals.
struct MixQuoteData
{
    std::int32_t glowDetail = 4; // 0x6c9dcc.
    std::int32_t map = -1;       // 0x6c9dd0.
    std::int32_t collapseType = -1;
    std::int32_t collapseMap = -1;
    std::int32_t collapseCounter = -1;
    float collapseDelay = -1.0f;
};
[[nodiscard]] Core::Expected<MixQuote, std::string> MixQuoteOf(const Npc::NpcWindowState& npc,
                                                               const MixQuoteData& data = {});

// 0x5b1130(n, buffer): n unsigned with the thousands separated by commas ("%d,%03d" and its siblings).
[[nodiscard]] std::string ThousandsText(std::uint32_t value);

// 0x5b12f0(price, buffer): the price plus the castle tax (0x78aba34 · price / 100, in signed 64 bits), with the
// thousands separated by commas.
[[nodiscard]] std::string TaxedPriceText(std::uint32_t price, std::int32_t taxRate);
} // namespace Client::ChaosMix
