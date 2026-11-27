#pragma once

#include "Core/Expected.h"
#include "Tables/ItemSetTables.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
struct OwnCharacterRecord;
} // namespace Client::Kernel

// The own record's set options: 0x40e1b0, which 0x550d10 calls first (0x550d24), with 0x40cfa0, 0x40d010 and 0x40de10,
// all thiscall on [0x6eed3c]; 0x40da20 is 0x40d690's code (Player::AddItemSetOptionValues, in ItemSetOptionList.h).
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los stats del registro propio", and 07, Q-C184 and Q-C187.
// What the original reads or writes outside its tables and lists is an explicit error, after what it does before.
namespace Client::Player
{
struct ItemSetOptionList;
struct ItemSetState;
struct OwnStatsContext;

// An entry of 0x40e1b0's local list, 3 bytes each: the set, how many of its pieces were counted and the last one's
// piece kind (byte +0x1c & 3 minus 1).
struct ItemSetPiece
{
    std::uint8_t setId = 0;
    std::uint8_t count = 0;
    std::uint8_t kind = 0;
};

// 0x40e1b0's buffer is 30 bytes (0x40e1bb..0x40e1d4), but 0x40cfa0 and 0x40d010 walk only the first four sets (up to
// offset 9, 0x40cfe0 and 0x40d467).
inline constexpr std::size_t ItemSetPieceSlots = 4;
using ItemSetPieces = std::array<ItemSetPiece, ItemSetPieceSlots>;

// 0x40cfa0: with the item's byte +0x1c & 3 at 1, 2 or 3, byte 0, 1 or 2 of ItemSetType.bmd's record `type` is its set;
// with 0 or 0xff, none. It adds it to the equal set or to the first free place of the four; with no place, nothing.
// `type` is the item's type, which the caller already checked is not negative. With 8,192 or more, the original reads
// outside the table: an error (Q-C187).
[[nodiscard]] Core::Expected<void, std::string>
AddItemSetPiece(ItemSetPieces& pieces, const Tables::ItemSetTypeTable& types, std::size_t type, std::uint8_t flags1c);

// 0x40d010: with each set of two pieces or more, the name, the ItemSetOption.bmd options its piece count gives and, if
// it is complete, the complete set's; the options from 0x18 on, only if the set marks the published hero's class. It
// leaves them in `state`'s list, piece kind 0's first, and writes the hero's +0x358.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemSetOptions(ItemSetState& state, const ItemSetPieces& pieces,
                                                                    const Tables::ItemSetOptionTable& options,
                                                                    Kernel::ActorState& publishedHero);

// 0x550d29..0x550d3f: the 16 marks of +0x937c at 0.
void ClearItemSetOptionMarks(ItemSetOptionList& list);

// The copies of the record's stats that 0x40e1b0 builds (0x40e429..0x40e453) to check each item's requirements in the
// second pass.
struct OwnStatCopies
{
    std::uint16_t strength = 0; // +0x18
    std::uint16_t agility = 0;  // +0x1a
    std::uint16_t vitality = 0; // +0x1c
    std::uint16_t energy = 0;   // +0x1e
};

// 0x40de10: what 0x550d10 does at its twelve places (0x40dc50, with 5 · value) and 0x40da20 with 0x8c to 0x8f, but on
// the copies: type 1 to the strength, 2 to the agility, 3 to the energy and 4 to the vitality. It opens and closes the
// record at each place.
[[nodiscard]] Core::Expected<void, std::string>
AddOwnBonusesToStatCopies(OwnStatCopies& copies, const Kernel::OwnCharacterRecord& own, const OwnStatsContext& context);

// 0x40e1b0: two passes over the equipment places except the wings and the pet, which count each set's pieces
// (0x40cfa0) and apply its options (0x40d010). Between them, 0x40de10 on the stat copies; the second pass skips the
// items whose requirements those copies do not meet. It opens and closes the record at each place and once in between.
// Without the tables, an error before touching anything.
[[nodiscard]] Core::Expected<void, std::string> RecalculateOwnItemSets(const Kernel::OwnCharacterRecord& own,
                                                                       const OwnStatsContext& context);
} // namespace Client::Player
