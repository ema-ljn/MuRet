#pragma once

#include "Core/Expected.h"

#include <bit>
#include <cstdint>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
}

namespace Client::Terrain
{
class ITerrainAttributeQuerySource;

// 0x6ba110: the −0.01f of the two fmul of 0x5f82c0. It is negative: the row and the column come out negative and
// 0x7d0d348 − index · 2 falls inside the table. With +0.01 only cell (0, 0) would be represented.
inline constexpr float TerrainAttributeQueryScale = std::bit_cast<float>(0xbc23d70au);

enum class TerrainAttributeQueryError
{
    UnrepresentedAddress
};

// The EAX/word projection of 0x5f82c0 (a closed contract): PC53/RN, invalid masked. Requires binary32/binary64, the
// host's RN, no fast-math and no contraction; it does not reproduce EDX, flags, status/traps or other x87 PC/RC. It
// does not configure the process's FP environment. The error marks a final address outside the borrowed table; it is
// not a native guard.
[[nodiscard]] Core::Expected<std::uint32_t, TerrainAttributeQueryError>
TerrainAttributeQueryAt(ITerrainAttributeQuerySource& source, float x, float y);
// A query of the canonical world with the factor of 0x6ba110, without an external mutable provider.
[[nodiscard]] Core::Expected<std::uint32_t, TerrainAttributeQueryError>
TerrainAttributeQueryAt(const Data::Terrain::TerrainAttributes& attributes, float x, float y);
} // namespace Client::Terrain
