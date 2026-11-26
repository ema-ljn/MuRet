#include "Data/Textures/JpegImage.h"

#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Each CI vector, decoded, must give the same bytes as the IJG libjpeg 6b, the original's
// (Client/tests/vectors/jpeg/README.md).
namespace
{
using Client::Data::DataError;
using Client::Data::Textures::DecodeJpeg;
using Client::Tests::ReadJpegVector;
using Client::Tests::SameBytes;

// A texture's.
constexpr int AnySide = Client::Data::Textures::TextureMaxSide;

// Its size, its bytes per pixel and, in <name>.6b.raw, the pixels 6b gives.
struct Vector
{
    std::string_view name;
    int width;
    int height;
    int components;
};

// The lab's three JPEG kinds (baseline 4:4:4 with a restart per MCU row, baseline 4:2:0 with sides that are not a
// block multiple, and progressive 4:4:4); a 4:4:0, which turbo upsamples unlike 6b unless the filter is off (Q-C33);
// a grayscale, a CMYK and a cut one; and one with a component at double height and another at double width, so
// narrow that 6b uses the filter on neither (Q-C33).
constexpr std::array<Vector, 8> DecodedBy6b{{
    {"baseline-444-dri", 16, 24, 3},
    {"baseline-420", 13, 11, 3},
    {"baseline-440", 16, 16, 3},
    {"progressive-444", 16, 16, 3},
    {"gray", 8, 8, 1},
    {"cmyk-3x2", 3, 2, 4},
    {"truncated", 16, 24, 3},
    {"mixed-sampling-narrow", 4, 16, 3},
}};

std::optional<DataError> DecodeError(std::span<const std::byte> jpeg, int maxSide)
{
    const auto decoded = DecodeJpeg(jpeg, maxSide);
    if (decoded.HasValue())
    {
        return std::nullopt;
    }
    return decoded.Error();
}

// Rows top to bottom: RGB, grayscale, or CMYK, which libjpeg does not convert to RGB and neither does the original.
TEST(JpegImageTest, DecodesLikeTheLibjpeg6b)
{
    for (const Vector& vector : DecodedBy6b)
    {
        SCOPED_TRACE(vector.name);
        const std::vector<std::byte> jpeg = ReadJpegVector(std::string(vector.name) + ".jpg");
        const std::vector<std::byte> expected = ReadJpegVector(std::string(vector.name) + ".6b.raw");

        const auto decoded = DecodeJpeg(jpeg, AnySide);

        ASSERT_TRUE(decoded.HasValue());
        EXPECT_EQ(decoded.Value().width, vector.width);
        EXPECT_EQ(decoded.Value().height, vector.height);
        EXPECT_EQ(decoded.Value().components, vector.components);
        EXPECT_TRUE(SameBytes(std::as_bytes(std::span(decoded.Value().pixels)), expected));
    }
}

// Cut in the middle of its data is not an error: libjpeg warns, completes the image and goes on, as in the original.
// turbo completes this one, the "truncated" vector above, like 6b; not every cut one comes out the same (Q-C33).
TEST(JpegImageTest, AJpegCutInTheMiddleOfItsDataStillDecodes)
{
    const std::vector<std::byte> whole = ReadJpegVector("baseline-444-dri.jpg");
    const std::vector<std::byte> cut = ReadJpegVector("truncated.jpg");
    ASSERT_LT(cut.size(), whole.size());
    ASSERT_TRUE(std::equal(cut.begin(), cut.end(), whole.begin()));

    EXPECT_EQ(DecodeError(cut, AnySide), std::nullopt);
}

// Nothing, a TGA (Q-C14), a JPEG that ends at its start marker, or one cut before its data.
TEST(JpegImageTest, WhatLibjpegCannotReadIsInvalidData)
{
    const std::vector<std::byte> whole = ReadJpegVector("baseline-444-dri.jpg");
    ASSERT_GT(whole.size(), 100u);
    const std::vector<std::byte> tga{std::byte{0}, std::byte{0}, std::byte{2}, std::byte{0}};
    const std::vector<std::byte> onlyTheStart(whole.begin(), whole.begin() + 2);
    const std::vector<std::byte> withoutData(whole.begin(), whole.begin() + 100);

    EXPECT_EQ(DecodeError({}, AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(tga, AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(onlyTheStart, AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(withoutData, AnySide), DataError::InvalidData);
}

// As in the original (Q-C33): 6b stops with JERR_ARITH_NOTIMPL, JERR_SOF_UNSUPPORTED and JERR_BAD_PRECISION, and with
// JERR_NO_HUFF_TABLE on the two without Huffman tables, missing in its only scan and in the second. turbo reads all
// but the 12-bit one, which the 8-bit API rejects later.
TEST(JpegImageTest, WhatThe6bCannotReadIsInvalidData)
{
    EXPECT_EQ(DecodeError(ReadJpegVector("arithmetic.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("lossless.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("precision-12.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("no-huffman-tables.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("no-huffman-tables-scan-2.jpg"), AnySide), DataError::InvalidData);
}

// Q-C33: with components at double height and others at double width 6b's result cannot be matched, and turbo's
// stays, with the filter on for all. If this fails because turbo gives something else, its upsampling changed:
// revisit UpsampleLikeThe6b.
TEST(JpegImageTest, ASamplingThatMixesDoubleHeightAndDoubleWidthDecodesLikeTurbo)
{
    const std::vector<std::byte> turbo = ReadJpegVector("mixed-sampling.turbo.raw");
    ASSERT_FALSE(SameBytes(turbo, ReadJpegVector("mixed-sampling.6b.raw")));

    const auto decoded = DecodeJpeg(ReadJpegVector("mixed-sampling.jpg"), AnySide);

    ASSERT_TRUE(decoded.HasValue());
    EXPECT_TRUE(SameBytes(std::as_bytes(std::span(decoded.Value().pixels)), turbo));
}

// A second SOI right before its EOI, the final FF D9.
std::vector<std::byte> WithASecondSoiAtTheEnd(std::string_view name)
{
    std::vector<std::byte> jpeg = ReadJpegVector(std::string(name) + ".jpg");
    if (jpeg.size() < 2 || jpeg.back() != std::byte{0xD9})
    {
        ADD_FAILURE() << name << " no termina en su EOI";
        return {};
    }
    jpeg.back() = std::byte{0xD8};
    jpeg.push_back(std::byte{0xFF});
    jpeg.push_back(std::byte{0xD9});
    return jpeg;
}

// Single scan (Q-C32): with a second SOI before the EOI, which jpeg_finish_decompress rejects, the image is the same.
// The original has used it by the time it rejects it.
TEST(JpegImageTest, WhatComesAfterTheLastRowIsNotRead)
{
    const std::vector<std::byte> expected = ReadJpegVector("baseline-444-dri.6b.raw");

    const auto decoded = DecodeJpeg(WithASecondSoiAtTheEnd("baseline-444-dri"), AnySide);

    ASSERT_TRUE(decoded.HasValue());
    EXPECT_TRUE(SameBytes(std::as_bytes(std::span(decoded.Value().pixels)), expected));
}

// Multi-scan: both libjpegs read everything in jpeg_start_decompress, and 6b stops on the second SOI
// (JERR_SOI_DUPLICATE): InvalidData.
TEST(JpegImageTest, AProgressiveJpegIsReadToTheEndBeforeItsFirstRow)
{
    EXPECT_EQ(DecodeError(WithASecondSoiAtTheEnd("progressive-444"), AnySide), DataError::InvalidData);
}

// Width and height are checked against maxSide separately. With 0 or less, nothing fits.
TEST(JpegImageTest, ASideOverTheLimitIsInvalidData)
{
    const std::vector<std::byte> wide = ReadJpegVector("flat-512x8.jpg");

    EXPECT_EQ(DecodeError(wide, 512), std::nullopt);
    EXPECT_EQ(DecodeError(wide, 511), DataError::InvalidData);
    EXPECT_EQ(DecodeError(wide, 0), DataError::InvalidData);
    EXPECT_EQ(DecodeError(wide, -1), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("flat-513x8.jpg"), 512), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("flat-8x513.jpg"), 512), DataError::InvalidData);
}
} // namespace
