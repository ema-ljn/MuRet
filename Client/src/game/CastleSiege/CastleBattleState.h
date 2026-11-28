#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// What the castle battle packets keep (CL9 milestone 13; 04 § Castle Siege, la batalla): the object of 0x5767924
// that B2 20 builds and 0x5a6fc0 draws, and the owner guild's mark from B9 02, which 0x5f1890 paints into the texture
// of the 30's flag (0x69c, Object31\Flag.tga).
namespace Client::CastleSiege
{
// 0x4e04f3: new(0x36), two halves of 0x1b with a whole B2 20 each: the first for key 0x13d and the second for the
// others (0x4e051e). In each half, +4/+5 the key, +6 shown (0x5a71f0), +7 the side (0x5a7215: 2, 3 or 4 against bits
// 0x80000, 0x400000 and 0x800000 of the hero's +0x78), +8 a text (8 bytes, *inferred* the guild) and +0x10 another
// (10 and the 0 of +0x1a, *inferred* the character). 0x5a6fc0 prints both texts with %s.
inline constexpr std::size_t SwitchHoldersSize = 0x36;
inline constexpr std::size_t SwitchHolderHalf = 0x1b;
inline constexpr std::uint16_t FirstHalfKey = 0x13d;
inline constexpr std::size_t HolderShownAt = 6;
inline constexpr std::size_t HolderSideAt = 7;
inline constexpr std::size_t HolderGuildAt = 8;
inline constexpr std::size_t HolderNameAt = 0x10;
inline constexpr std::size_t HolderEndAt = 0x1a;

// B9 02 (0x4e0790): 32 bytes, two 4-bit colors per byte (the even one high), 8 × 8.
inline constexpr std::size_t OwnerMarkColors = 64;

struct CastleBattleState
{
    // What 0x4e0790 unpacks for 0x5f1890(0x69c, colors, 1), and how many times it arrived: the texture is rebuilt with
    // each B9 02 (P-C372).
    std::array<std::uint8_t, OwnerMarkColors> ownerMark{};
    std::uint32_t ownerMarkRevision = 0;
};
} // namespace Client::CastleSiege
