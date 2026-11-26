#include "Data/Terrain/TerrainAttributes.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Data/DataError.h"
#include "Crypto/K16.h"
#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace Client::Data::Terrain
{
namespace
{
// The plain header is 00 N FF FF (005f8390-encterrain-att.md, "Formato"): the first byte and the last two are fixed,
// and the middle one is the world, checked separately.
constexpr std::byte HeaderStart{0x00};
constexpr std::byte HeaderEnd{0xFF};

bool IsTheHeader(std::span<const std::byte> plain)
{
    return plain[0] == HeaderStart && plain[2] == HeaderEnd && plain[3] == HeaderEnd;
}

struct DecodedAttributes
{
    std::unique_ptr<TerrainAttributes> attributes;
    std::uint8_t world = 0;
};

Core::Expected<DecodedAttributes, DataError> DecodeTerrainAttributes(std::span<const std::byte> file)
{
    if (file.size() != TerrainAttributesSizeWithByteCells && file.size() != TerrainAttributesSizeWithWordCells)
    {
        return Core::Unexpected(DataError::UnexpectedSize);
    }

    std::vector<std::byte> plain(file.begin(), file.end());
    Crypto::DecryptK16(plain);
    Crypto::Xor3(plain);
    if (!IsTheHeader(plain))
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    // With two bytes per cell, the low one comes first (little-endian) and the high one is dropped.
    const std::size_t cellSize = file.size() == TerrainAttributesSizeWithWordCells ? 2 : 1;
    auto attributes = std::make_unique<TerrainAttributes>();
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        const auto low = std::to_integer<std::uint16_t>(plain[TerrainAttributesHeaderSize + cell * cellSize]);
        if (low >= TerrainAttributeLimit)
        {
            return Core::Unexpected(DataError::InvalidData);
        }
        attributes->cells[cell] = low;
    }

    return DecodedAttributes{.attributes = std::move(attributes),
                             .world = std::to_integer<std::uint8_t>(plain[TerrainAttributesWorldOffset])};
}
} // namespace

Core::Expected<std::unique_ptr<TerrainAttributes>, DataError> LoadTerrainAttributes(std::span<const std::byte> file,
                                                                                    std::uint8_t world)
{
    auto decoded = DecodeTerrainAttributes(file);
    if (!decoded.HasValue())
        return Core::Unexpected(decoded.Error());
    if (decoded.Value().world != world)
    {
        return Core::Unexpected(DataError::WrongWorld);
    }
    return std::move(decoded.Value().attributes);
}

Core::Expected<std::unique_ptr<TerrainAttributes>, DataError>
LoadTerrainAttributesOfAnyWorld(std::span<const std::byte> file)
{
    auto decoded = DecodeTerrainAttributes(file);
    if (!decoded.HasValue())
        return Core::Unexpected(decoded.Error());
    return std::move(decoded.Value().attributes);
}
} // namespace Client::Data::Terrain
