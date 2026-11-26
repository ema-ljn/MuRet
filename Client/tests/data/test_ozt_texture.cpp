#include "Data/Textures/OztTexture.h"

#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
using Client::Data::DataError;
using Client::Data::Textures::LoadOztTexture;
using Client::Data::Textures::OztBitsPerPixelOffset;
using Client::Data::Textures::OztHeaderSize;
using Client::Data::Textures::OztPixelsOffset;
using Client::Data::Textures::OztPrefixSize;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Tests::FilePixel;
using Client::Tests::PlainOzt;
using Client::Tests::TgaHeaderSize;

static_assert(OztPixelsOffset == OztPrefixSize + TgaHeaderSize);

// y = 0 is the first row uploaded to OpenGL.
std::array<std::uint8_t, 4> TexturePixel(const Texture& texture, int x, int y)
{
    const auto at =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.textureWidth) + static_cast<std::size_t>(x)) *
        4;
    return {texture.pixels[at], texture.pixels[at + 1], texture.pixels[at + 2], texture.pixels[at + 3]};
}

// RGBA, in row height - 1 - y.
std::array<std::uint8_t, 4> Rgba(int x, int y)
{
    const std::array<std::uint8_t, 4> bgra = FilePixel(x, y);
    return {bgra[2], bgra[1], bgra[0], bgra[3]};
}

// The rest zero.
void ExpectTheImage(const Texture& texture)
{
    ASSERT_EQ(texture.pixels.size(), static_cast<std::size_t>(texture.textureWidth * texture.textureHeight) * 4);
    for (int y = 0; y < texture.textureHeight; ++y)
    {
        for (int x = 0; x < texture.textureWidth; ++x)
        {
            const bool inTheImage = x < texture.width && y < texture.height;
            const std::array<std::uint8_t, 4> expected =
                inTheImage ? Rgba(x, texture.height - 1 - y) : std::array<std::uint8_t, 4>{};
            EXPECT_EQ(TexturePixel(texture, x, y), expected) << "(" << x << ", " << y << ")";
        }
    }
}

std::optional<DataError> LoadError(const std::vector<std::byte>& file)
{
    const auto loaded = LoadOztTexture(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

TEST(OztTextureTest, FlipsTheRowsAndPutsRedFirst)
{
    const auto loaded = LoadOztTexture(PlainOzt(4, 2));

    ASSERT_TRUE(loaded.HasValue());
    const Texture& texture = loaded.Value();
    EXPECT_EQ(texture.width, 4);
    EXPECT_EQ(texture.height, 2);
    EXPECT_EQ(texture.textureWidth, 4);
    EXPECT_EQ(texture.textureHeight, 2);
    EXPECT_EQ(texture.format, PixelFormat::Rgba);
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(texture));
    const std::array<std::uint8_t, 4> lastPixelOfTheFile = FilePixel(3, 1);
    EXPECT_EQ(TexturePixel(texture, 3, 0),
              (std::array{lastPixelOfTheFile[2], lastPixelOfTheFile[1], lastPixelOfTheFile[0], lastPixelOfTheFile[3]}));
}

// Unscaled, the rest zero.
TEST(OztTextureTest, AnImageOfOtherSidesGoesInTheCornerOfThePowerOf2)
{
    const auto loaded = LoadOztTexture(PlainOzt(3, 5));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().textureWidth, 4);
    EXPECT_EQ(loaded.Value().textureHeight, 8);
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}

// As in the original (cmp al,0x20).
TEST(OztTextureTest, OnlyAccepts32BitsPerPixel)
{
    for (const std::uint8_t bitsPerPixel : {std::uint8_t{0}, std::uint8_t{8}, std::uint8_t{24}, std::uint8_t{33}})
    {
        EXPECT_EQ(LoadError(PlainOzt(2, 2, bitsPerPixel)), DataError::InvalidData) << int{bitsPerPixel} << " bits";
    }
}

// The largest the original accepts, and a tall non-power-of-2 one with more rows than a byte holds.
TEST(OztTextureTest, ALargeImageKeepsEveryPixel)
{
    for (const auto& [width, height] : {std::array<std::int16_t, 2>{512, 512}, std::array<std::int16_t, 2>{5, 300}})
    {
        const auto loaded = LoadOztTexture(PlainOzt(width, height));

        ASSERT_TRUE(loaded.HasValue()) << width << " × " << height;
        ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value())) << width << " × " << height;
    }
}

TEST(OztTextureTest, AcceptsSidesUpTo512)
{
    EXPECT_EQ(LoadError(PlainOzt(512, 1)), std::nullopt);
    EXPECT_EQ(LoadError(PlainOzt(1, 512)), std::nullopt);
    EXPECT_EQ(LoadError(PlainOzt(513, 1)), DataError::InvalidData);
    EXPECT_EQ(LoadError(PlainOzt(1, 513)), DataError::InvalidData);
}

// The original compares width and height signed: zero or negative pass, and it copies nothing.
TEST(OztTextureTest, ZeroOrANegativeSideLoadsWithoutPixels)
{
    for (const auto& [width, height] : {std::array<std::int16_t, 2>{0, 4}, std::array<std::int16_t, 2>{4, 0},
                                        std::array<std::int16_t, 2>{-4, 4}, std::array<std::int16_t, 2>{4, -32768}})
    {
        const std::vector<std::byte> file = PlainOzt(width, height);
        ASSERT_EQ(file.size(), OztPixelsOffset) << width << " × " << height;

        const auto loaded = LoadOztTexture(file);

        ASSERT_TRUE(loaded.HasValue()) << width << " × " << height;
        EXPECT_EQ(loaded.Value().width, width);
        EXPECT_EQ(loaded.Value().height, height);
        EXPECT_EQ(loaded.Value().textureWidth, width > 0 ? 4 : 1);
        EXPECT_EQ(loaded.Value().textureHeight, height > 0 ? 4 : 1);
        // The original allocates and uploads the buffer even if it copies nothing.
        ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value())) << width << " × " << height;
    }
}

// The original ignores the prefix and the TGA fields other than size and bits per pixel: the id length, the image
// type (10 would be compressed) and the descriptor (0x28 would put the origin on top). It still reads uncompressed
// pixels from 0x16 and flips the rows.
TEST(OztTextureTest, ReadsOnlyTheSizeAndTheBitsPerPixel)
{
    std::vector<std::byte> file = PlainOzt(3, 2);
    for (std::size_t prefix = 0; prefix < OztPrefixSize; ++prefix)
    {
        file[prefix] = std::byte{0xA5};
    }
    file[OztPrefixSize] = std::byte{5};      // the id length
    file[OztPrefixSize + 2] = std::byte{10}; // the image type
    file[OztBitsPerPixelOffset + 1] = std::byte{0x28};

    const auto loaded = LoadOztTexture(file);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}

// Q-C24: the original ignores the size. Missing: the header, the end of the TGA's (the unread bytes before the
// pixels), all the pixels or one.
TEST(OztTextureTest, AShortFileIsAnError)
{
    const std::vector<std::byte> whole = PlainOzt(2, 3);
    for (const std::size_t size :
         {std::size_t{0}, OztHeaderSize - 1, OztHeaderSize, OztPixelsOffset - 1, OztPixelsOffset, whole.size() - 1})
    {
        const std::vector<std::byte> file(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(size));

        EXPECT_EQ(LoadError(file), DataError::FileTooShort) << size << " bytes";
    }
}

// As in the original, which rejects the lab's 24-bit OZT even if pixels are missing.
TEST(OztTextureTest, TheBitsPerPixelComeBeforeTheMissingPixels)
{
    std::vector<std::byte> file = PlainOzt(2, 2, 24);
    file.resize(OztHeaderSize);

    EXPECT_EQ(LoadError(file), DataError::InvalidData);
}

// Like the lab TGAs' 26-byte footer.
TEST(OztTextureTest, IgnoresTheBytesAfterThePixels)
{
    std::vector<std::byte> file = PlainOzt(2, 2);
    file.resize(file.size() + 26, std::byte{0x5A});

    const auto loaded = LoadOztTexture(file);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}
} // namespace
