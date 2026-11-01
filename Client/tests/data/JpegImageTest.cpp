// DecodeJpeg contra los vectores de CI: cada uno, decodificado, tiene que dar los mismos bytes que
// la IJG libjpeg 6b, la del original (Client/tests/vectors/jpeg/README.md).

#include "Data/Textures/JpegImage.h"

#include "Data/DataError.h"
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

namespace
{

using Client::Data::DataError;
using Client::Data::Textures::DecodeJpeg;
using Client::Tests::ReadJpegVector;
using Client::Tests::SameBytes;

// El lado más grande que se pide en estos tests: el de una textura (TextureMaxSide).
constexpr int AnySide = 512;

// Un vector y lo que tiene que dar: su tamaño, sus bytes por píxel y, en <nombre>.6b.raw, los
// píxeles que da la 6b.
struct Vector
{
    std::string_view name;
    int width;
    int height;
    int components;
};

// Los tres tipos de JPEG del laboratorio (baseline 4:4:4 con un reinicio por fila de MCU, baseline
// 4:2:0 de lados que no son múltiplo del bloque, y progresivo 4:4:4); un 4:4:0, que turbo agranda
// distinto que la 6b si no se le apaga el filtro (Q-C33); uno en grises, uno CMYK y uno cortado.
constexpr std::array<Vector, 7> DecodedBy6b{{
    {"baseline-444-dri", 16, 24, 3},
    {"baseline-420", 13, 11, 3},
    {"baseline-440", 16, 16, 3},
    {"progressive-444", 16, 16, 3},
    {"gray", 8, 8, 1},
    {"cmyk-3x2", 3, 2, 4},
    {"truncated", 16, 24, 3},
}};

// El error de decodificar `jpeg`, o std::nullopt si decodificó.
std::optional<DataError> DecodeError(std::span<const std::byte> jpeg, int maxSide)
{
    const auto decoded = DecodeJpeg(jpeg, maxSide);
    if (decoded.HasValue())
    {
        return std::nullopt;
    }
    return decoded.Error();
}

// Cada vector da los mismos píxeles que la 6b, con las filas de arriba hacia abajo: en RGB, en
// grises o en CMYK, que libjpeg no pasa a RGB, y el original tampoco.
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

// Un JPEG cortado en el medio de los datos no es un error: libjpeg avisa, completa la imagen como
// la 6b (el vector "truncated" de arriba) y sigue, como en el original. Es un baseline: un
// progresivo cortado, turbo lo completa distinto (Q-C33).
TEST(JpegImageTest, AJpegCutInTheMiddleOfItsDataStillDecodes)
{
    const std::vector<std::byte> whole = ReadJpegVector("baseline-444-dri.jpg");
    const std::vector<std::byte> cut = ReadJpegVector("truncated.jpg");
    ASSERT_LT(cut.size(), whole.size());
    ASSERT_TRUE(std::equal(cut.begin(), cut.end(), whole.begin()));

    EXPECT_EQ(DecodeError(cut, AnySide), std::nullopt);
}

// Lo que libjpeg no puede leer es InvalidData: nada, un TGA (Q-C14), un JPEG que termina en su
// marcador de inicio, o uno cortado antes de sus datos.
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

// Lo que la 6b no lee y turbo sí es InvalidData, como en el original (Q-C33): la 6b corta con
// JERR_ARITH_NOTIMPL, JERR_SOF_UNSUPPORTED y JERR_BAD_PRECISION.
TEST(JpegImageTest, WhatThe6bCannotReadIsInvalidData)
{
    EXPECT_EQ(DecodeError(ReadJpegVector("arithmetic.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("lossless.jpg"), AnySide), DataError::InvalidData);
    EXPECT_EQ(DecodeError(ReadJpegVector("precision-12.jpg"), AnySide), DataError::InvalidData);
}

// Lo que viene después de la última fila no se lee (Q-C32): con un segundo SOI en vez del EOI, que
// jpeg_finish_decompress rechaza, la imagen es la misma. El original ya la usó cuando lo rechaza.
TEST(JpegImageTest, WhatComesAfterTheLastRowIsNotRead)
{
    std::vector<std::byte> jpeg = ReadJpegVector("baseline-444-dri.jpg");
    const std::vector<std::byte> expected = ReadJpegVector("baseline-444-dri.6b.raw");
    ASSERT_GT(jpeg.size(), 2u);
    ASSERT_EQ(jpeg.back(), std::byte{0xD9});
    jpeg.back() = std::byte{0xD8};
    jpeg.insert(jpeg.end(), {std::byte{0xFF}, std::byte{0xD9}});

    const auto decoded = DecodeJpeg(jpeg, AnySide);

    ASSERT_TRUE(decoded.HasValue());
    EXPECT_TRUE(SameBytes(std::as_bytes(std::span(decoded.Value().pixels)), expected));
}

// El ancho y el alto se miran contra maxSide, cada uno por su lado. Con 0 o menos, nada entra.
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
