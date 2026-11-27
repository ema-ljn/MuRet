#pragma once

#include "Tables/ItemTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

// The item state of 0x54bf40 and the earlier fields of 0x5bc740. Neither a C layout nor a wire decoder.
namespace Client::Kernel
{
// MuRet's own maximum, with stable metadata and a valid 0x4225d0 boundary: nine options.
inline constexpr std::size_t MaxItemOptions = 9;
inline constexpr std::size_t ItemOptionValueOffset = 8; // D+0x33 versus D+0x2b
inline constexpr std::size_t ItemOptionStorageSize = ItemOptionValueOffset + MaxItemOptions;

struct ItemState
{
    std::uint16_t id = 0;
    std::uint32_t scalar1Low = 0; // D+4: zero-extended copy of B1's low 8 bits
    std::uint8_t itemClass = 0;
    // Neutral names: the contract credits offsets and calculations, not stat names.
    std::uint8_t byte0a = 0;
    std::uint16_t word0c = 0;
    std::uint16_t word0e = 0;
    std::uint8_t byte10 = 0;
    std::uint16_t word12 = 0;
    std::uint16_t word14 = 0;
    std::uint8_t byte16 = 0;
    std::uint8_t byte17 = 0;
    std::uint16_t word18 = 0;
    std::uint8_t durability = 0; // D+1a, set by 0x5bc740, not by 0x54bf40
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
    std::uint16_t word1e = 0;
    std::uint16_t word20 = 0;
    std::uint16_t word22 = 0;
    std::uint16_t word24 = 0;
    std::uint16_t word26 = 0;
    std::uint16_t word28 = 0;
    std::uint8_t optionCount = 0;
    // One region D+2b..3b: tag8 overwrites value0. Tag-only values keep their history.
    std::array<std::uint8_t, ItemOptionStorageSize> optionBytes{};
    std::uint32_t occupied = 0;  // D+3c, the initializer does not touch it either
    std::uint8_t gridColumn = 0; // D+42/43, only grid producers
    std::uint8_t gridRow = 0;
    // D+0x44: the equipped item's warning the HUD leaves (0x5aac20): 8 broken, 7, 6 and 5 by the durability left and 3
    // without the requirements. The 23 that drops an equipped item resets it (0x4d11f5); the inventory reads it
    // (milestone 4).
    std::uint8_t warning = 0;

    // Precondition: index < MaxItemOptions, which does not imply index < optionCount. An invalid index stops with a
    // diagnostic, in Release too.
    [[nodiscard]] std::uint8_t OptionTag(std::uint8_t index) const;
    [[nodiscard]] std::uint8_t OptionValue(std::uint8_t index) const;
};

struct ItemInitializationScalars
{
    std::uint32_t b1 = 0;
    std::uint32_t b3 = 0;
    std::uint32_t b4 = 0;
};

// The only opaque boundary, synchronous at 0x54cc5c. Not called for other IDs.
using Item4225d0 = std::function<void(ItemState&)>;

// 0x54bf40's own behavior, complete. The ID already set and its metadata alive and not aliasing the destination; it
// adds no sentinel guard or rollback the original lacks. The whole B3 matters for ID180b/L>6. contextWord is S+e's
// word, reread only if the late family asks for it. It may refer to D+e: what is seen is its value after the own
// calculations, not an input snapshot. The upper 24 bits of this machine's addresses are not invented.
// For ID1a04, apply4225d0 must exist and leave a valid state of its contract: the ID kept, count0/1->1/2 and ordinary
// bookkeeping not aliasing the metadata. MuRet's is Scenes::AddTheDarkHorseOption, which opens the own record (06, "El
// F3 10, el inventario"). An empty function on that branch breaks the precondition and throws; it is not a silent
// success.
void InitializeItemState(ItemState& item, std::span<const std::byte, Tables::ItemRecordSize> metadata,
                         ItemInitializationScalars scalars, const std::uint16_t& contextWord,
                         const Item4225d0& apply4225d0);

// Whether InitializeItemState reads contextWord with this ID and this B3: the weapons' late family (0 to 0xbff, 0x1a0c,
// 0x1a0d and 0x1a19 to 0x1a1c, signed) with bit 0x10 of B3's low byte. In the original it is the own record's level,
// [0x7681eb0] + 0xe (0x54cd97 and 0x54cdec), and the caller knows whether it has the record open (06, "El F3 10, el
// inventario").
[[nodiscard]] bool ReadsTheContextWord(std::uint16_t id, std::uint32_t b3);
} // namespace Client::Kernel
