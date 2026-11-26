#pragma once

#include "Crypto/Xor3.h"
#include "Crypto/K16.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"
#include "support/LittleEndian.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Helpers for the terrain tests: build a synthetic file and encrypt it as the original's exporter does, so that the
// loaders decrypt it, and compare a layer with a memory dump.
namespace Client::Tests
{
// The worlds the lab's Data\ has, by the N of their World<N> folder
// (Client/docs/research/09-datos-disponibles.md). All have their .map, .att and .obj.
inline constexpr std::array<std::uint8_t, 20> LabWorlds = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                                           11, 12, 19, 25, 31, 32, 34, 35, 55, 56};
// The ones with TerrainHeight.OZB: all but World6, which has none (09, "Terreno").
inline constexpr std::array<std::uint8_t, 19> LabWorldsWithHeights = {1,  2,  3,  4,  5,  7,  8,  9,  10, 11,
                                                                      12, 19, 25, 31, 32, 34, 35, 55, 56};

// The inverse of DecryptK16, like the one the original's exporter uses (0x5f8960):
// encrypted[i] = (plain[i] + previous) ^ K16Key[i % 16], with the same `previous` as decryption.
inline std::vector<std::byte> EncryptK16(std::vector<std::byte> bytes)
{
    using Crypto::K16Key;
    std::uint8_t previous = Crypto::K16Seed;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto plain = std::to_integer<std::uint8_t>(bytes[i]);
        const auto key = std::to_integer<std::uint8_t>(K16Key[i % K16Key.size()]);
        const auto encrypted = static_cast<std::uint8_t>(static_cast<std::uint8_t>(plain + previous) ^ key);
        bytes[i] = std::byte{encrypted};
        previous = static_cast<std::uint8_t>(encrypted + Crypto::K16Step);
    }
    return bytes;
}

// A value per cell that repeats every `period` cells. With a prime like 251, 241 or 239 it does not repeat every row
// (256) or every layer (0x10000): a layer read shifted or in place of another gives other values, and the test
// notices.
inline std::uint8_t CellPattern(std::size_t cell, std::size_t period, std::uint8_t offset)
{
    return static_cast<std::uint8_t>(cell % period + offset);
}

inline std::uint8_t Layer1Pattern(std::size_t cell)
{
    return CellPattern(cell, 251, 0);
}

inline std::uint8_t Layer2Pattern(std::size_t cell)
{
    return CellPattern(cell, 241, 7);
}

inline std::uint8_t AlphaPattern(std::size_t cell)
{
    return CellPattern(cell, 239, 3);
}

// A plain EncTerrain.map: version 0, world `world` and each layer with its pattern.
inline std::vector<std::byte> PlainTerrainMapping(std::uint8_t world)
{
    using Data::Terrain::TerrainCellCount;
    using Data::Terrain::TerrainMappingAlphaOffset;
    using Data::Terrain::TerrainMappingLayer1Offset;
    using Data::Terrain::TerrainMappingLayer2Offset;
    std::vector<std::byte> plain(Data::Terrain::TerrainMappingSize);
    plain[Data::Terrain::TerrainMappingWorldOffset] = std::byte{world};
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        plain[TerrainMappingLayer1Offset + cell] = std::byte{Layer1Pattern(cell)};
        plain[TerrainMappingLayer2Offset + cell] = std::byte{Layer2Pattern(cell)};
        plain[TerrainMappingAlphaOffset + cell] = std::byte{AlphaPattern(cell)};
    }
    return plain;
}

// One attribute per cell, always below TerrainAttributeLimit: 127 values that do not repeat every row. For a WORD's
// high byte, another pattern that is never zero, to see that it is dropped.
inline std::uint8_t AttributePattern(std::size_t cell)
{
    return CellPattern(cell, 127, 0);
}

inline std::uint8_t AttributeHighBytePattern(std::size_t cell)
{
    return CellPattern(cell, 13, 1);
}

// A plain EncTerrain.att: header 00 `world` FF FF and each cell with AttributePattern, in a byte or in a WORD with
// AttributeHighBytePattern on top.
enum class AttributeCells
{
    Bytes,
    Words,
};

inline std::vector<std::byte> PlainTerrainAttributes(std::uint8_t world, AttributeCells cells)
{
    using Data::Terrain::TerrainAttributesHeaderSize;
    using Data::Terrain::TerrainAttributesSizeWithByteCells;
    using Data::Terrain::TerrainAttributesSizeWithWordCells;
    using Data::Terrain::TerrainAttributesWorldOffset;
    using Data::Terrain::TerrainCellCount;
    const bool words = cells == AttributeCells::Words;
    std::vector<std::byte> plain(words ? TerrainAttributesSizeWithWordCells : TerrainAttributesSizeWithByteCells);
    plain[0] = std::byte{0x00};
    plain[TerrainAttributesWorldOffset] = std::byte{world};
    plain[2] = std::byte{0xFF};
    plain[3] = std::byte{0xFF};
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        if (words)
        {
            plain[TerrainAttributesHeaderSize + 2 * cell] = std::byte{AttributePattern(cell)};
            plain[TerrainAttributesHeaderSize + 2 * cell + 1] = std::byte{AttributeHighBytePattern(cell)};
        }
        else
        {
            plain[TerrainAttributesHeaderSize + cell] = std::byte{AttributePattern(cell)};
        }
    }
    return plain;
}

// The .att encryption, the reverse of decryption: XOR3 first and then K16.
inline std::vector<std::byte> EncryptTerrainAttributes(std::vector<std::byte> plain)
{
    Crypto::Xor3(plain);
    return EncryptK16(std::move(plain));
}

// The type and the seven floats.
inline constexpr std::size_t ObjectValueCount = 8;

// An object's values as the bits the file stores. Two objects are the same if they have the same bits, also with a
// NaN, which is not equal to itself.
inline std::array<std::uint32_t, ObjectValueCount> ObjectBits(const Data::Terrain::TerrainObject& object)
{
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    return {std::bit_cast<std::uint16_t>(object.type),
            bits(object.position[0]),
            bits(object.position[1]),
            bits(object.position[2]),
            bits(object.angle[0]),
            bits(object.angle[1]),
            bits(object.angle[2]),
            bits(object.scale)};
}

// `count` objects with all their values distinct from each other and from one object to the next: if the loader
// reads a field in place of another, or shifts objects, the test notices. They are exact floats (multiples of 1/8), so
// that the test can write them as numbers.
inline std::vector<Data::Terrain::TerrainObject> SampleTerrainObjects(std::size_t count)
{
    std::vector<Data::Terrain::TerrainObject> objects;
    for (std::size_t k = 0; k < count; ++k)
    {
        const auto base = static_cast<float>(k) * 100.0f;
        objects.push_back({.type = static_cast<std::int16_t>(k + 1),
                           .position = {base + 1.5f, base + 2.25f, base + 3.125f},
                           .angle = {base + 4.5f, base + 5.25f, -(base + 6.125f)},
                           .scale = base + 0.875f});
    }
    return objects;
}

// A plain EncTerrain.obj: version 0, world `world`, count `count` and `objects`. The count goes separately to be able
// to write one that is not the real one: negative, or more than there are.
inline std::vector<std::byte> PlainTerrainObjects(std::uint8_t world, std::int16_t count,
                                                  std::span<const Data::Terrain::TerrainObject> objects)
{
    std::vector<std::byte> plain{std::byte{0x00}, std::byte{world}};
    AppendLittleEndian(plain, std::bit_cast<std::uint16_t>(count), sizeof(count));
    for (const Data::Terrain::TerrainObject& object : objects)
    {
        const std::array<std::uint32_t, ObjectValueCount> bits = ObjectBits(object);
        AppendLittleEndian(plain, bits[0], sizeof(object.type));
        for (std::size_t value = 1; value < bits.size(); ++value)
        {
            AppendLittleEndian(plain, bits[value], sizeof(float));
        }
    }
    return plain;
}

// Each cell's height in a synthetic TerrainHeight.OZB. With 251 it does not repeat every row: a shifted row gives
// other values.
inline std::uint8_t HeightPattern(std::size_t cell)
{
    return CellPattern(cell, 251, 0);
}

// The size of an 8-bit BMP header with a 256-color palette: the two headers of 14 and 40 bytes and 256 colors of 4.
// It is the bfOffBits of the lab's 17 8-bit TerrainHeight.OZB, 2 bytes less than what the original reads
// (005f9020-terrainheight-ozb.md §6).
inline constexpr std::size_t StandardBitmapHeaderSize = 14 + 40 + 256 * 4;
static_assert(StandardBitmapHeaderSize == 0x436, "el bfOffBits que mide el research (005f9020 §6)");
// Where bfOffBits is in a BMP: after bfType (2 bytes), bfSize (4) and two reserved fields of 2. It is 4 little-endian
// bytes.
inline constexpr std::size_t BitmapOffBitsOffset = 2 + 4 + 2 + 2;
static_assert(BitmapOffBitsOffset == 10, "el layout de BITMAPFILEHEADER");

// A TerrainHeight.OZB: the prefix and the header with a byte not in HeightPattern (0xFF), and the heights with
// HeightPattern from TerrainHeightsValuesOffset. If the loader read from elsewhere, cell 0 or the last one would come
// out with another value.
inline std::vector<std::byte> PlainTerrainHeights()
{
    using Data::Terrain::TerrainCellCount;
    using Data::Terrain::TerrainHeightsValuesOffset;
    std::vector<std::byte> file(Data::Terrain::TerrainHeightsSize, std::byte{0xFF});
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        file[TerrainHeightsValuesOffset + cell] = std::byte{HeightPattern(cell)};
    }
    return file;
}

// The first cell where `layer` is not `expected`, or std::nullopt if they are equal. Compares each value's bytes: for
// alpha, the four of each float, in the original's memory order (little-endian, like x86). If `expected` lacks bytes,
// the answer is the first incomplete cell; if it has extra, TerrainCellCount, the cell after the last one.
template <typename T>
std::optional<std::size_t> FirstCellThatDiffers(const Data::Terrain::TerrainLayer<T>& layer,
                                                std::span<const std::byte> expected)
{
    static_assert(std::endian::native == std::endian::little, "los volcados son de un x86");
    const std::span<const std::byte> mine = std::as_bytes(std::span(layer));
    for (std::size_t cell = 0; cell < layer.size(); ++cell)
    {
        const std::size_t begin = cell * sizeof(T);
        if (expected.size() < begin + sizeof(T))
        {
            return cell;
        }
        for (std::size_t i = begin; i < begin + sizeof(T); ++i)
        {
            if (mine[i] != expected[i])
            {
                return cell;
            }
        }
    }
    if (expected.size() != mine.size())
    {
        return layer.size();
    }
    return std::nullopt;
}
} // namespace Client::Tests
