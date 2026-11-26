#include "Data/Terrain/TerrainLight.h"

#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Textures/JpegImage.h"
#include "Data/Textures/OzjTexture.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// OZJs built from the CI vectors. What is tested is the light: each byte times 1/255 and the rows flipped, against
// what DecodeJpeg gives (its tests are in test_jpeg_image.cpp), and the accepted size.
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
using Client::Tests::SameBytes;

std::optional<DataError> LoadError(std::span<const std::byte> file)
{
    const auto loaded = LoadTerrainLight(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// Cell (x, y) is pixel x of the JPEG's row 255 - y, each channel times the constant.
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

// Q-C31: only a 256 × 256 color light fits the original's table. A smaller side (height or width), a larger one or a
// grayscale one are InvalidData.
TEST(TerrainLightTest, OnlyTakesALightOf256By256InColor)
{
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("light-256x255.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("light-255x256.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-512x8.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("light-gray-256.jpg"))), DataError::InvalidData);
}

// Q-C32: in a single-scan JPEG, like light-256, what follows the last row is not read. With a second SOI before the
// EOI, which jpeg_finish_decompress rejects, the light is the same: the original has written the table by then.
TEST(TerrainLightTest, WhatComesAfterTheLastRowIsNotRead)
{
    std::vector<std::byte> jpeg = ReadJpegVector("light-256.jpg");
    const auto expected = LoadTerrainLight(OzjOf(jpeg));
    ASSERT_TRUE(expected.HasValue());
    ASSERT_EQ(jpeg.back(), std::byte{0xD9});
    jpeg.back() = std::byte{0xD8};
    jpeg.insert(jpeg.end(), {std::byte{0xFF}, std::byte{0xD9}});

    const auto light = LoadTerrainLight(OzjOf(jpeg));

    ASSERT_TRUE(light.HasValue());
    EXPECT_TRUE(
        SameBytes(std::as_bytes(std::span(light.Value()->cells)), std::as_bytes(std::span(expected.Value()->cells))));
}

// What is not a JPEG is InvalidData.
TEST(TerrainLightTest, AFileWithoutJpegIsTooShort)
{
    const std::vector<std::byte> file = OzjOf(ReadJpegVector("light-256.jpg"));
    ASSERT_GT(file.size(), OzjPrefixSize + 1);

    EXPECT_EQ(LoadError({}), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize)), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize + 1)), DataError::InvalidData);
}
} // namespace
