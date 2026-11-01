// LoadTerrainLight con OZJ armados desde los vectores de CI. Lo que se prueba es lo de la luz:
// cada byte por 1/255 y las filas dadas vuelta, contra lo que da DecodeJpeg (sus tests están en
// JpegImageTest.cpp), y el tamaño que se acepta.

#include "Data/Terrain/TerrainLight.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Textures/JpegImage.h"
#include "Data/Textures/OzjTexture.h"
#include "support/JpegVectors.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{

using Client::Data::DataError;
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::LoadTerrainLight;
using Client::Data::Terrain::TerrainLightChannels;
using Client::Data::Terrain::TerrainLightScale;
using Client::Data::Terrain::TerrainSide;
using Client::Data::Textures::DecodeJpeg;
using Client::Data::Textures::OzjPrefixSize;
using Client::Tests::OzjOf;
using Client::Tests::ReadJpegVector;

// El error de cargar `file`, o std::nullopt si cargó.
std::optional<DataError> LoadError(std::span<const std::byte> file)
{
    const auto loaded = LoadTerrainLight(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// La celda (x, y) es el píxel x de la fila 255 - y del JPEG, con cada canal por la constante.
TEST(TerrainLightTest, EachCellIsAPixelOfTheFlippedImageTimesTheScale)
{
    const std::vector<std::byte> jpeg = ReadJpegVector("light-256.jpg");
    const auto image = DecodeJpeg(jpeg, static_cast<int>(TerrainSide));
    ASSERT_TRUE(image.HasValue());

    const auto light = LoadTerrainLight(OzjOf(jpeg));

    ASSERT_TRUE(light.HasValue());
    const std::vector<std::uint8_t>& pixels = image.Value().pixels;
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            const auto& color =
                light.Value()->cells[CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))];
            const std::size_t pixel = ((TerrainSide - 1 - y) * TerrainSide + x) * TerrainLightChannels;
            for (std::size_t channel = 0; channel < TerrainLightChannels; ++channel)
            {
                const float expected = static_cast<float>(pixels[pixel + channel]) * TerrainLightScale;
                ASSERT_EQ(std::bit_cast<std::uint32_t>(color[channel]), std::bit_cast<std::uint32_t>(expected))
                    << "(" << x << ", " << y << "), canal " << channel;
            }
        }
    }
}

// Q-C31: solo entra una luz de 256 × 256 en color, la que llena la tabla del original. Una más
// chica, una más grande o una en grises son InvalidData.
TEST(TerrainLightTest, OnlyTakesALightOf256By256InColor)
{
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("light-256x255.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-512x8.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("light-gray-256.jpg"))), DataError::InvalidData);
}

// Sin bytes después de los 24 no hay JPEG: FileTooShort. Lo que no es un JPEG es InvalidData.
TEST(TerrainLightTest, AFileWithoutJpegIsTooShort)
{
    const std::vector<std::byte> file = OzjOf(ReadJpegVector("light-256.jpg"));
    ASSERT_GT(file.size(), OzjPrefixSize + 1);

    EXPECT_EQ(LoadError({}), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize)), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize + 1)), DataError::InvalidData);
}

} // namespace
