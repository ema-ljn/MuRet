// Ayudas para los tests del terreno: armar un archivo sintético y cifrarlo como lo hace el
// exportador del original, para que los cargadores lo descifren, y comparar una capa con un
// volcado de memoria.

#pragma once

#include "Data/Tables/Xor3.h"
#include "Data/Terrain/K16.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{

// La inversa de DecryptK16, como la que usa el exportador del original (0x5f8960):
// cifrado[i] = (plano[i] + previo) ^ K16Key[i % 16], con el mismo `previo` que el descifrado.
inline std::vector<std::byte> EncryptK16(std::vector<std::byte> bytes)
{
    using Data::Terrain::K16Key;
    std::uint8_t previous = Data::Terrain::K16Seed;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto plain = std::to_integer<std::uint8_t>(bytes[i]);
        const auto key = std::to_integer<std::uint8_t>(K16Key[i % K16Key.size()]);
        const auto encrypted = static_cast<std::uint8_t>(static_cast<std::uint8_t>(plain + previous) ^ key);
        bytes[i] = std::byte{encrypted};
        previous = static_cast<std::uint8_t>(encrypted + Data::Terrain::K16Step);
    }
    return bytes;
}

// Un valor por celda que se repite cada `period` celdas. Con un primo como 251, 241 o 239 no se
// repite cada fila (256) ni cada capa (0x10000): una capa leída corrida o en el lugar de otra da
// otros valores, y el test lo nota.
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

// Un EncTerrain.map en claro: versión 0, el mundo `world` y cada capa con su patrón.
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

// Un atributo por celda, siempre debajo de TerrainAttributeLimit: 127 valores que no se repiten
// cada fila. Para el byte de arriba de un WORD, otro patrón que nunca es cero, para ver que se
// descarta.
inline std::uint8_t AttributePattern(std::size_t cell)
{
    return CellPattern(cell, 127, 0);
}

inline std::uint8_t AttributeHighBytePattern(std::size_t cell)
{
    return CellPattern(cell, 13, 1);
}

// Un EncTerrain.att en claro: la cabecera 00 `world` FF FF y cada celda con AttributePattern, en un
// byte o en un WORD con AttributeHighBytePattern arriba.
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

// El cifrado del .att, al revés que el descifrado: primero XOR3 y después K16.
inline std::vector<std::byte> EncryptTerrainAttributes(std::vector<std::byte> plain)
{
    Data::Tables::Xor3(plain);
    return EncryptK16(std::move(plain));
}

// La primera celda en la que `layer` no es `expected`, o std::nullopt si son iguales. Compara
// los bytes de cada valor: para el alfa, los cuatro de cada float, en el orden de la memoria del
// original (little-endian, como x86). Si a `expected` le faltan bytes, la respuesta es la primera
// celda incompleta; si le sobran, TerrainCellCount, la celda que vendría después de la última.
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
