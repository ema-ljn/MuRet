#include "Data/Textures/OzjTexture.h"

#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// OZJs built from the CI vectors. DecodeJpeg's own tests are in test_jpeg_image.cpp; here, the OZJ part: the 24
// unread bytes, the power-of-2 texture, the unflipped rows and the rejections.
namespace
{
using Client::Data::DataError;
using Client::Data::Textures::LoadOzjTexture;
using Client::Data::Textures::OzjPrefixSize;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Tests::OzjOf;
using Client::Tests::OzjRepeatedBytes;
using Client::Tests::ReadJpegVector;
using Client::Tests::SameBytes;

static_assert(OzjRepeatedBytes == OzjPrefixSize);

constexpr std::size_t RgbSize = Client::Data::Textures::BytesPerPixel(PixelFormat::Rgb);

std::optional<DataError> LoadError(std::span<const std::byte> file)
{
    const auto loaded = LoadOzjTexture(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// Each row of `image` (`rowSize` bytes, as 6b gives them) at the start of its texture row, in order, and the rest of
// the texture zero.
void ExpectTheRows(const Texture& texture, std::span<const std::byte> image, std::size_t rowSize)
{
    const std::size_t textureRowSize = static_cast<std::size_t>(texture.textureWidth) * RgbSize;
    ASSERT_EQ(texture.pixels.size(), textureRowSize * static_cast<std::size_t>(texture.textureHeight));
    const auto pixels = std::as_bytes(std::span(texture.pixels));
    for (std::size_t row = 0; row < static_cast<std::size_t>(texture.textureHeight); ++row)
    {
        const auto textureRow = pixels.subspan(row * textureRowSize, textureRowSize);
        std::vector<std::byte> expected(textureRowSize);
        if (row < static_cast<std::size_t>(texture.height))
        {
            const auto imageRow = image.subspan(row * rowSize, rowSize);
            std::copy(imageRow.begin(), imageRow.end(), expected.begin());
        }
        EXPECT_TRUE(SameBytes(textureRow, expected)) << "fila " << row;
    }
}

// RGB, rows in JPEG order: the OZJ does not flip them.
TEST(OzjTextureTest, KeepsTheRowsOfTheJpeg)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("baseline-444-dri.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().format, PixelFormat::Rgb);
    EXPECT_EQ(loaded.Value().width, 16);
    EXPECT_EQ(loaded.Value().height, 24);
    EXPECT_EQ(loaded.Value().textureWidth, 16);
    EXPECT_EQ(loaded.Value().textureHeight, 32);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("baseline-444-dri.6b.raw"), 16 * RgbSize));
}

// Unscaled in the corner of a 16 × 16 texture, the rest zero (Q-C30).
TEST(OzjTextureTest, AnImageOfOtherSidesGoesInTheCornerOfThePowerOf2)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("baseline-420.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().textureWidth, 16);
    EXPECT_EQ(loaded.Value().textureHeight, 16);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("baseline-420.6b.raw"), 13 * RgbSize));
}

// Other leading bytes give the same texture.
TEST(OzjTextureTest, TheBytesBeforeTheJpegAreNotRead)
{
    std::vector<std::byte> file = OzjOf(ReadJpegVector("baseline-444-dri.jpg"));
    ASSERT_GT(file.size(), OzjPrefixSize);
    for (std::size_t at = 0; at < OzjPrefixSize; ++at)
    {
        file[at] = std::byte{0xAA};
    }

    const auto loaded = LoadOzjTexture(file);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("baseline-444-dri.6b.raw"), 16 * RgbSize));
}

// Q-C31: a grayscale JPEG gives one byte per pixel, and the original copies that row as is to the start of a
// 3-byte-per-pixel one: it fills a third, one gray per byte.
TEST(OzjTextureTest, AGrayJpegFillsAThirdOfEachRow)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("gray.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().format, PixelFormat::Rgb);
    EXPECT_EQ(loaded.Value().textureWidth, 8);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("gray.6b.raw"), 8));
}

// Q-C31: a CMYK JPEG gives 4 bytes per pixel, and the original copies the row as is. It fits if the width is at most
// 3/4 of the texture's: cmyk-3x2's 3 pixels are 12 bytes, 4 texture pixels. OpenGL reads them as RGB.
TEST(OzjTextureTest, ACmykRowThatFitsIsCopiedAsIs)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("cmyk-3x2.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().format, PixelFormat::Rgb);
    EXPECT_EQ(loaded.Value().textureWidth, 4);
    EXPECT_EQ(loaded.Value().textureHeight, 2);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("cmyk-3x2.6b.raw"), 3 * 4));
}

// Q-C31: if the row does not fit, like cmyk-4x2's 4 CMYK pixels (16 bytes) in a 4-wide texture (12), the original
// writes into the next row and, on the last, past the buffer. MuRet does not load it.
TEST(OzjTextureTest, ARowThatDoesNotFitIsInvalidData)
{
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("cmyk-4x2.jpg"))), DataError::InvalidData);
}

// As in the original: 0x63b870 compares each side with 0x200 unsigned (CMP EAX,0x200 and JA, at 0x63bb17 and
// 0x63bb28).
TEST(OzjTextureTest, AcceptsSidesUpTo512)
{
    const auto wide = LoadOzjTexture(OzjOf(ReadJpegVector("flat-512x8.jpg")));
    ASSERT_TRUE(wide.HasValue());
    EXPECT_EQ(wide.Value().textureWidth, 512);
    EXPECT_EQ(wide.Value().textureHeight, 8);

    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-513x8.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-8x513.jpg"))), DataError::InvalidData);
}

// With a single byte there is already something to give libjpeg, which cannot read it.
TEST(OzjTextureTest, AFileWithoutJpegIsTooShort)
{
    const std::vector<std::byte> file = OzjOf(ReadJpegVector("baseline-444-dri.jpg"));
    ASSERT_GT(file.size(), OzjPrefixSize + 1);

    EXPECT_EQ(LoadError({}), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize)), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize + 1)), DataError::InvalidData);
}

// Q-C14: seven lab OZJs are TGA. libjpeg rejects them ("Not a JPEG file"), as in the original.
TEST(OzjTextureTest, ATgaIsNotAJpeg)
{
    // An uncompressed 2 × 2 TGA with its 18-byte header, as those seven start.
    std::vector<std::byte> tga(18 + 2 * 2 * 4);
    tga[2] = std::byte{2};

    EXPECT_EQ(LoadError(OzjOf(tga)), DataError::InvalidData);
}
} // namespace
