#include "Data/Terrain/TerrainAttributes.h"

#include "Core/ByteReader.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/TerrainFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// EncTerrain.att reales: los de la carpeta golden y, si está, el volcado de la memoria del original
// (Client/tests/support/GoldenDir.h).
namespace
{
using Client::Core::ByteReader;
using Client::Data::Terrain::LoadTerrainAttributes;
using Client::Data::Terrain::LoadTerrainMapping;
using Client::Data::Terrain::TerrainAttributes;
using Client::Data::Terrain::TerrainAttributesSizeWithByteCells;
using Client::Data::Terrain::TerrainAttributesSizeWithWordCells;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainSide;
using Client::Tests::DeviasCapture;
using Client::Tests::LabWorlds;
using Client::Tests::LorenciaCapture;
using Client::Tests::MemoryCapture;
using Client::Tests::ReadFileBytes;

using GoldenTerrainAttributes = Client::Tests::GoldenDataTest;

// La ruta que arma el original: "Data\\%s\\EncTerrain%d.att" con "World%d" (0x6c7314, en 0x61a560).
std::string AttributesFile(std::uint8_t world)
{
    const std::string number = std::to_string(world);
    return "World" + number + "\\EncTerrain" + number + ".att";
}

// En LabWorlds, el 1 y los del 31 en adelante tienen dos bytes por celda; el resto, uno. El 31 y el
// 56 tienen celdas con el byte de arriba distinto de cero, que se descarta.

TEST_F(GoldenTerrainAttributes, LoadsEveryWorldOfTheLab)
{
    for (const std::uint8_t world : LabWorlds)
    {
        const auto path = DataFile(AttributesFile(world));
        ASSERT_TRUE(path.has_value()) << "mundo " << int{world};
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "mundo " << int{world};
        const bool words = world == 1 || world >= 31;
        EXPECT_EQ(file->size(), words ? TerrainAttributesSizeWithWordCells : TerrainAttributesSizeWithByteCells)
            << "mundo " << int{world};

        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), world);

        EXPECT_TRUE(loaded.HasValue()) << "mundo " << int{world};
    }
}

// Los .att con otro número en el nombre, que el original elige según el estado del juego
// (0x61a6ea, 0x61a710). Su cabecera dice el número de la carpeta, no el del nombre.
TEST_F(GoldenTerrainAttributes, LoadsTheVariantsWithTheWorldOfTheirFolder)
{
    struct Variant
    {
        const char* path;
        std::uint8_t world;
    };
    constexpr std::array<Variant, 3> variants{{
        {"World31\\EncTerrain312.att", 31},
        {"World35\\EncTerrain351.att", 35},
        {"World35\\EncTerrain352.att", 35},
    }};
    for (const Variant& variant : variants)
    {
        const auto path = DataFile(variant.path);
        ASSERT_TRUE(path.has_value()) << variant.path;
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << variant.path;

        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), variant.world);

        EXPECT_TRUE(loaded.HasValue()) << variant.path;
    }
}

// El .att de Lorencia es de dos bytes por celda, y en él se ve si el cargador se queda con el byte
// correcto: de las 7.939 celdas de agua (el tile 5 de la capa 1 del .map), 7.818 tienen el
// atributo 4. Es el contraste que hizo el research entre los dos archivos
// (005f87b0-encterrain-map.md §8). Con el byte de arriba, que en Lorencia es siempre cero, darían 0.
TEST_F(GoldenTerrainAttributes, LorenciaWaterHasAttribute4)
{
    const auto attributesPath = DataFile(AttributesFile(1));
    const auto mappingPath = DataFile("World1\\EncTerrain1.map");
    ASSERT_TRUE(attributesPath.has_value());
    ASSERT_TRUE(mappingPath.has_value());
    const std::optional<std::vector<char>> attributesFile = ReadFileBytes(*attributesPath);
    const std::optional<std::vector<char>> mappingFile = ReadFileBytes(*mappingPath);
    ASSERT_TRUE(attributesFile.has_value());
    ASSERT_TRUE(mappingFile.has_value());

    const auto attributes = LoadTerrainAttributes(std::as_bytes(std::span(*attributesFile)), 1);
    const auto mapping = LoadTerrainMapping(std::as_bytes(std::span(*mappingFile)), 1);

    ASSERT_TRUE(attributes.HasValue());
    ASSERT_TRUE(mapping.HasValue());
    constexpr std::uint8_t Water = 5;
    std::size_t water = 0;
    std::size_t waterWithAttribute4 = 0;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        if (mapping.Value()->layer1[cell] == Water)
        {
            ++water;
            if (attributes.Value()->cells[cell] == 4)
            {
                ++waterWithAttribute4;
            }
        }
    }
    EXPECT_EQ(water, 7939u);
    EXPECT_EQ(waterWithAttribute4, 7818u);
}

// Una celda que el juego cambió después de cargar: dónde está, qué trae el archivo y qué tenía la
// memoria del original.
struct ChangedCell
{
    std::uint8_t x;
    std::uint8_t y;
    std::uint16_t file;
    std::uint16_t memory;

    bool operator==(const ChangedCell&) const = default;
};

void PrintTo(const ChangedCell& cell, std::ostream* out)
{
    *out << "(" << int{cell.x} << ", " << int{cell.y} << "): " << cell.file << " -> " << cell.memory;
}

// Las celdas en las que los atributos que carga MuRet no son los del volcado `memory`, los WORD de
// 0x07D0D348, en el orden de las celdas, fila por fila. Una celda que falta en el volcado cuenta
// como distinta, con 0 en memoria.
std::vector<ChangedCell> CompareWithMemory(const TerrainAttributes& attributes, std::span<const std::byte> memory)
{
    ByteReader reader(memory);
    std::vector<ChangedCell> changed;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        const std::uint16_t inMemory = reader.ReadU16Le().value_or(0);
        if (attributes.cells[cell] != inMemory)
        {
            const auto x = static_cast<std::uint8_t>(cell % TerrainSide);
            const auto y = static_cast<std::uint8_t>(cell / TerrainSide);
            changed.push_back({x, y, attributes.cells[cell], inMemory});
        }
    }
    return changed;
}

class MemoryTerrainAttributes : public Client::Tests::MemoryDumpTest
{
protected:
    // Las celdas que difieren entre el .att de la captura y su volcado `name`. Marca el test como
    // fallado si falta algo.
    std::optional<std::vector<ChangedCell>> CellsThatDiffer(const MemoryCapture& capture, std::string_view name) const
    {
        const auto path = DataFile(AttributesFile(capture.world));
        const auto dump = DumpFile(capture, name);
        if (!path || !dump)
        {
            return std::nullopt;
        }
        const std::optional<std::vector<char>> file = ReadFileBytes(*path);
        const std::optional<std::vector<char>> memory = ReadFileBytes(*dump);
        if (!file || !memory)
        {
            ADD_FAILURE() << "no se pudo leer " << (file ? *dump : *path);
            return std::nullopt;
        }
        if (memory->size() != TerrainCellCount * sizeof(std::uint16_t))
        {
            ADD_FAILURE() << name << " mide " << memory->size() << " bytes, no "
                          << TerrainCellCount * sizeof(std::uint16_t);
            return std::nullopt;
        }
        const auto loaded = LoadTerrainAttributes(std::as_bytes(std::span(*file)), capture.world);
        if (!loaded.HasValue())
        {
            ADD_FAILURE() << AttributesFile(capture.world) << " no cargó";
            return std::nullopt;
        }
        return CompareWithMemory(*loaded.Value(), std::as_bytes(std::span(*memory)));
    }
};

// Los atributos son los que tenía en memoria el main.exe original después de cargar el mismo
// archivo, en Devias (World3; s31_att.bin), salvo 10 celdas que el juego ya había cambiado cuando
// se leyó la memoria. Son las que lista el research (s31-assets/byte-a-byte.md): el bit 0x02 de la
// celda del personaje, puesto en (199, 19) y limpiado en (9, 79), y el 0x04 limpiado en dos
// pasajes de 4 celdas.
TEST_F(MemoryTerrainAttributes, EqualsTheOriginalsMemoryInDeviasButForTheGameState)
{
    const auto changed = CellsThatDiffer(DeviasCapture, "s31_att.bin");

    ASSERT_TRUE(changed.has_value());
    const std::vector<ChangedCell> gameState = {
        {199, 19, 1, 3},  {9, 79, 3, 1},   {118, 115, 4, 0}, {119, 115, 4, 0}, {120, 115, 4, 0},
        {121, 115, 4, 0}, {92, 205, 4, 0}, {93, 205, 4, 0},  {94, 205, 4, 0},  {95, 205, 4, 0},
    };
    EXPECT_EQ(*changed, gameState);
}

// Lo mismo en Lorencia (World1; lor_att.bin), salvo 7 celdas (byte-a-byte.md, el segundo mapa): el
// bit 0x02 puesto en (141, 117) y (130, 121) y limpiado en (221, 11), y el 0x04 limpiado en las
// mismas 4 celdas de (118..121, 115) que en Devias, aunque es otro mapa.
TEST_F(MemoryTerrainAttributes, EqualsTheOriginalsMemoryInLorenciaButForTheGameState)
{
    const auto changed = CellsThatDiffer(LorenciaCapture, "lor_att.bin");

    ASSERT_TRUE(changed.has_value());
    const std::vector<ChangedCell> gameState = {
        {221, 11, 2, 0},  {118, 115, 5, 1}, {119, 115, 5, 1}, {120, 115, 5, 1},
        {121, 115, 5, 1}, {141, 117, 1, 3}, {130, 121, 1, 3},
    };
    EXPECT_EQ(*changed, gameState);
}
} // namespace
