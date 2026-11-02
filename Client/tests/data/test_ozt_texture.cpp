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

// Client::Data::Textures::LoadOztTexture: OZT, con archivos sintéticos.
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

// El píxel RGBA (x, y) de la textura, con y = 0 la primera fila que se sube a OpenGL.
std::array<std::uint8_t, 4> TexturePixel(const Texture& texture, int x, int y)
{
    const auto at =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.textureWidth) + static_cast<std::size_t>(x)) *
        4;
    return {texture.pixels[at], texture.pixels[at + 1], texture.pixels[at + 2], texture.pixels[at + 3]};
}

// Lo que se espera en la textura para el píxel (x, y) del archivo: en RGBA y en la fila alto - 1 - y.
std::array<std::uint8_t, 4> Rgba(int x, int y)
{
    const std::array<std::uint8_t, 4> bgra = FilePixel(x, y);
    return {bgra[2], bgra[1], bgra[0], bgra[3]};
}

// Cada píxel del archivo en su lugar de la textura, y el resto en cero.
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

// El error de cargar `file`, o std::nullopt si cargó.
std::optional<DataError> LoadError(const std::vector<std::byte>& file)
{
    const auto loaded = LoadOztTexture(file);
    if (loaded.HasValue())
    {
        return std::nullopt;
    }
    return loaded.Error();
}

// Las filas se dan vuelta y cada píxel pasa de BGRA a RGBA.
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

// Una imagen que no es de potencias de 2 queda en la esquina de una textura que sí lo es, sin
// escalar, y el resto queda en cero.
TEST(OztTextureTest, AnImageOfOtherSidesGoesInTheCornerOfThePowerOf2)
{
    const auto loaded = LoadOztTexture(PlainOzt(3, 5));

    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().textureWidth, 4);
    EXPECT_EQ(loaded.Value().textureHeight, 8);
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}

// Solo se aceptan 32 bits por píxel, como en el original (cmp al,0x20).
TEST(OztTextureTest, OnlyAccepts32BitsPerPixel)
{
    for (const std::uint8_t bitsPerPixel : {std::uint8_t{0}, std::uint8_t{8}, std::uint8_t{24}, std::uint8_t{33}})
    {
        EXPECT_EQ(LoadError(PlainOzt(2, 2, bitsPerPixel)), DataError::InvalidData) << int{bitsPerPixel} << " bits";
    }
}

// En una imagen grande, cada píxel sigue en su lugar: la más grande que acepta el original, y una
// alta que no es de potencias de 2, con más filas que las que entran en un byte.
TEST(OztTextureTest, ALargeImageKeepsEveryPixel)
{
    for (const auto& [width, height] : {std::array<std::int16_t, 2>{512, 512}, std::array<std::int16_t, 2>{5, 300}})
    {
        const auto loaded = LoadOztTexture(PlainOzt(width, height));

        ASSERT_TRUE(loaded.HasValue()) << width << " × " << height;
        ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value())) << width << " × " << height;
    }
}

// Un lado de 512 entra; uno de 513, no.
TEST(OztTextureTest, AcceptsSidesUpTo512)
{
    EXPECT_EQ(LoadError(PlainOzt(512, 1)), std::nullopt);
    EXPECT_EQ(LoadError(PlainOzt(1, 512)), std::nullopt);
    EXPECT_EQ(LoadError(PlainOzt(513, 1)), DataError::InvalidData);
    EXPECT_EQ(LoadError(PlainOzt(1, 513)), DataError::InvalidData);
}

// El original compara el ancho y el alto con signo: cero o negativo pasan, y no copia nada.
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
        // El buffer está igual, entero y en cero: el original lo pide y lo sube aunque no copie nada.
        ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value())) << width << " × " << height;
    }
}

// El original no mira el prefijo ni los campos del TGA que no son el tamaño y los bits por píxel:
// el largo del id, el tipo de imagen (10 sería comprimido) ni el descriptor (0x28 diría que el
// origen está arriba). Lee igual píxeles sin comprimir desde 0x16 y da vuelta las filas.
TEST(OztTextureTest, ReadsOnlyTheSizeAndTheBitsPerPixel)
{
    std::vector<std::byte> file = PlainOzt(3, 2);
    for (std::size_t prefix = 0; prefix < OztPrefixSize; ++prefix)
    {
        file[prefix] = std::byte{0xA5};
    }
    file[OztPrefixSize] = std::byte{5};      // el largo del id
    file[OztPrefixSize + 2] = std::byte{10}; // el tipo de imagen
    file[OztBitsPerPixelOffset + 1] = std::byte{0x28};

    const auto loaded = LoadOztTexture(file);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}

// Q-C24: el original no mira el tamaño. Falta la cabecera, el final de la del TGA (los bytes que
// no se leen antes de los píxeles), todos los píxeles o uno.
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

// La cabecera completa alcanza para rechazar: los bits por píxel se miran antes que los píxeles,
// como en el original, que rechaza el OZT de 24 bits del laboratorio aunque le falten píxeles.
TEST(OztTextureTest, TheBitsPerPixelComeBeforeTheMissingPixels)
{
    std::vector<std::byte> file = PlainOzt(2, 2, 24);
    file.resize(OztHeaderSize);

    EXPECT_EQ(LoadError(file), DataError::InvalidData);
}

// Lo que sigue a los píxeles se ignora, como el pie de 26 bytes de los TGA del laboratorio.
TEST(OztTextureTest, IgnoresTheBytesAfterThePixels)
{
    std::vector<std::byte> file = PlainOzt(2, 2);
    file.resize(file.size() + 26, std::byte{0x5A});

    const auto loaded = LoadOztTexture(file);

    ASSERT_TRUE(loaded.HasValue());
    ASSERT_NO_FATAL_FAILURE(ExpectTheImage(loaded.Value()));
}
} // namespace
