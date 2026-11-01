// LoadOzjTexture con OZJ armados desde los vectores de CI: el JPEG lo decodifica DecodeJpeg (sus
// tests están en JpegImageTest.cpp), y acá se prueba lo del OZJ: los 24 bytes que no se leen, la
// textura de potencias de 2, las filas sin dar vuelta y los rechazos.

#include "Data/Textures/OzjTexture.h"

#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "support/JpegVectors.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

// Los bytes RGB de una textura, de a filas.
constexpr std::size_t RgbSize = 3;

// El error de cargar `file`, o std::nullopt si cargó.
std::optional<DataError> LoadError(std::span<const std::byte> file)
{
    const auto loaded = LoadOzjTexture(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// Cada fila de `image` (de `rowSize` bytes, como las da la 6b) al principio de su fila de la
// textura, en el mismo orden, y el resto de la textura en cero.
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

// Los píxeles de la 6b, en RGB y con las filas en el orden del JPEG: el OZJ no las da vuelta.
TEST(OzjTextureTest, KeepsTheRowsOfTheJpeg)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("baseline-444-dri.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().format, PixelFormat::Rgb);
    EXPECT_EQ(loaded.Value().width, 16);
    EXPECT_EQ(loaded.Value().height, 8);
    EXPECT_EQ(loaded.Value().textureWidth, 16);
    EXPECT_EQ(loaded.Value().textureHeight, 8);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("baseline-444-dri.6b.raw"), 16 * RgbSize));
}

// Una imagen de 13 × 11 queda en la esquina de una textura de 16 × 16, sin escalar, y el resto en
// cero (Q-C30).
TEST(OzjTextureTest, AnImageOfOtherSidesGoesInTheCornerOfThePowerOf2)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("baseline-420.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().textureWidth, 16);
    EXPECT_EQ(loaded.Value().textureHeight, 16);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("baseline-420.6b.raw"), 13 * RgbSize));
}

// Los 24 bytes del principio no se leen: con otros, la textura es la misma.
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

// Q-C31: un JPEG en grises da un byte por píxel, y el original copia esa fila tal cual al
// principio de una de 3 bytes por píxel: llena un tercio, con un gris por byte.
TEST(OzjTextureTest, AGrayJpegFillsAThirdOfEachRow)
{
    const auto loaded = LoadOzjTexture(OzjOf(ReadJpegVector("gray.jpg")));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().format, PixelFormat::Rgb);
    EXPECT_EQ(loaded.Value().textureWidth, 8);
    ASSERT_NO_FATAL_FAILURE(ExpectTheRows(loaded.Value(), ReadJpegVector("gray.6b.raw"), 8));
}

// Q-C31: un JPEG CMYK da 4 bytes por píxel, y su fila no entra en la de la textura: el original
// escribe en la fila siguiente y, en la última, fuera del buffer. MuRet no lo carga.
TEST(OzjTextureTest, ARowThatDoesNotFitIsInvalidData)
{
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("cmyk-4x2.jpg"))), DataError::InvalidData);
}

// Un lado de 512 entra; uno de 513, no, como en el original (0x63b870 compara con 0x201).
TEST(OzjTextureTest, AcceptsSidesUpTo512)
{
    const auto wide = LoadOzjTexture(OzjOf(ReadJpegVector("flat-512x8.jpg")));
    ASSERT_TRUE(wide.HasValue());
    EXPECT_EQ(wide.Value().textureWidth, 512);
    EXPECT_EQ(wide.Value().textureHeight, 8);

    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-513x8.jpg"))), DataError::InvalidData);
    EXPECT_EQ(LoadError(OzjOf(ReadJpegVector("flat-8x513.jpg"))), DataError::InvalidData);
}

// Sin bytes después de los 24 no hay JPEG: FileTooShort. Con uno solo ya hay qué pasarle a
// libjpeg, que no lo puede leer.
TEST(OzjTextureTest, AFileWithoutJpegIsTooShort)
{
    const std::vector<std::byte> file = OzjOf(ReadJpegVector("baseline-444-dri.jpg"));
    ASSERT_GT(file.size(), OzjPrefixSize + 1);

    EXPECT_EQ(LoadError({}), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize)), DataError::FileTooShort);
    EXPECT_EQ(LoadError(std::span(file).first(OzjPrefixSize + 1)), DataError::InvalidData);
}

// Q-C14: siete OZJ del laboratorio son TGA. libjpeg los rechaza ("Not a JPEG file"), como en el
// original, y la textura no carga.
TEST(OzjTextureTest, ATgaIsNotAJpeg)
{
    // Un TGA sin comprimir de 2 × 2 con su cabecera de 18 bytes, como empiezan esos siete.
    std::vector<std::byte> tga(18 + 2 * 2 * 4);
    tga[2] = std::byte{2};

    EXPECT_EQ(LoadError(OzjOf(tga)), DataError::InvalidData);
}

} // namespace
