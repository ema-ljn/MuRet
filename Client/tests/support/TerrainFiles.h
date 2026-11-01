// Ayudas para los tests del terreno: armar un archivo sintético y cifrarlo como lo hace el
// exportador del original, para que los cargadores lo descifren, y comparar una capa con un
// volcado de memoria.

#pragma once

#include "Data/Tables/Xor3.h"
#include "Data/Terrain/K16.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Data/Terrain/TerrainObjects.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{

// Los mundos que trae el Data\ del laboratorio, por el N de su carpeta World<N>
// (Client/docs/research/09-datos-disponibles.md). Todos tienen su .map, su .att y su .obj.
inline constexpr std::array<std::uint8_t, 20> LabWorlds = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                                           11, 12, 19, 25, 31, 32, 34, 35, 55, 56};
// Los que traen TerrainHeight.OZB: todos menos World6, que no tiene (09, "Terreno").
inline constexpr std::array<std::uint8_t, 19> LabWorldsWithHeights = {1,  2,  3,  4,  5,  7,  8,  9,  10, 11,
                                                                      12, 19, 25, 31, 32, 34, 35, 55, 56};

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

// Agrega los `size` bytes de abajo de `value` a `bytes`, el menos significativo primero, como los
// escribe el x86 del original.
inline void AppendLittleEndian(std::vector<std::byte>& bytes, std::uint32_t value, std::size_t size)
{
    constexpr unsigned BitsPerByte = 8;
    for (std::size_t byte = 0; byte < size; ++byte)
    {
        bytes.push_back(static_cast<std::byte>(value >> (BitsPerByte * byte)));
    }
}

// Cuántos valores tiene un objeto: el tipo y los siete floats.
inline constexpr std::size_t ObjectValueCount = 8;

// Los valores de un objeto como los bits que guarda el archivo. Dos objetos son el mismo si tienen
// los mismos bits, también con un NaN, que no es igual a sí mismo.
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

// `count` objetos con todos sus valores distintos entre sí y de un objeto al otro: si el
// cargador lee un campo en el lugar de otro, o se corre de objeto, el test lo nota. Son floats
// justos (múltiplos de 1/8), para que el test pueda escribirlos como números.
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

// Un EncTerrain.obj en claro: versión 0, el mundo `world`, la cantidad `count` y `objects`. La
// cantidad va aparte para poder escribir una que no es la de verdad: negativa, o más de los que
// hay.
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

// La altura de cada celda en un TerrainHeight.OZB sintético. Con 251 no se repite cada fila: una
// fila corrida da otros valores.
inline std::uint8_t HeightPattern(std::size_t cell)
{
    return CellPattern(cell, 251, 0);
}

// Lo que ocupa la cabecera de un BMP de 8 bits con paleta de 256 colores: las dos cabeceras de 14 y
// 40 bytes y 256 colores de 4. Es el bfOffBits de los 17 TerrainHeight.OZB de 8 bits del
// laboratorio, 2 bytes menos de lo que lee el original (005f9020-terrainheight-ozb.md §6).
inline constexpr std::size_t StandardBitmapHeaderSize = 14 + 40 + 256 * 4;
static_assert(StandardBitmapHeaderSize == 0x436, "el bfOffBits que mide el research (005f9020 §6)");
// Dónde está bfOffBits en un BMP: después de bfType (2 bytes), bfSize (4) y dos campos reservados de
// 2. Son 4 bytes little-endian.
inline constexpr std::size_t BitmapOffBitsOffset = 2 + 4 + 2 + 2;
static_assert(BitmapOffBitsOffset == 10, "el layout de BITMAPFILEHEADER");

// Un TerrainHeight.OZB: el prefijo y la cabecera con un byte que no está en HeightPattern (0xFF),
// y las alturas con HeightPattern desde TerrainHeightsValuesOffset. Si el cargador leyera desde
// otro lugar, la celda 0 o la última saldrían con otro valor.
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
