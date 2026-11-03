#include "Data/Textures/OztTexture.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Data::Textures
{
namespace
{
using Core::ByteReader;

// El orden de los bytes de un píxel en el archivo: azul, verde, rojo y alfa, como los guarda un
// TGA de 32 bits.
constexpr std::size_t FileBlue = 0;
constexpr std::size_t FileGreen = 1;
constexpr std::size_t FileRed = 2;
constexpr std::size_t FileAlpha = 3;

// Los tres campos de la cabecera que lee el original. El ancho y el alto van con signo, como los
// compara él.
struct OztHeader
{
    int width = 0;
    int height = 0;
    std::uint8_t bitsPerPixel = 0;
};

// Los campos van seguidos, así que se leen de corrido desde el ancho.
static_assert(OztHeightOffset == OztWidthOffset + sizeof(std::int16_t) &&
                  OztBitsPerPixelOffset == OztHeightOffset + sizeof(std::int16_t),
              "el ancho, el alto y los bits por píxel van seguidos (0063bd00-ozt-loader.md)");

// La cabecera de `file`, o std::nullopt si el archivo no llega a OztHeaderSize.
std::optional<OztHeader> ReadHeader(std::span<const std::byte> file)
{
    if (file.size() < OztHeaderSize)
    {
        return std::nullopt;
    }
    ByteReader reader(file.subspan(OztWidthOffset));
    const std::optional<std::uint16_t> width = reader.ReadU16Le();
    const std::optional<std::uint16_t> height = reader.ReadU16Le();
    const std::optional<std::uint8_t> bitsPerPixel = reader.ReadU8();
    if (!width || !height || !bitsPerPixel)
    {
        return std::nullopt;
    }
    return OztHeader{std::bit_cast<std::int16_t>(*width), std::bit_cast<std::int16_t>(*height), *bitsPerPixel};
}

// Copia la fila `row` de la imagen, que empieza en `source`, a la fila alto - 1 - row de la
// textura, pasando cada píxel de BGRA a RGBA.
void CopyRow(std::span<const std::byte> source, Texture& texture, int row)
{
    const std::size_t pixelSize = BytesPerPixel(PixelFormat::Rgba);
    const auto destinationRow = static_cast<std::size_t>(texture.height - 1 - row);
    std::uint8_t* destination =
        texture.pixels.data() + destinationRow * static_cast<std::size_t>(texture.textureWidth) * pixelSize;
    for (std::size_t pixel = 0; pixel < source.size(); pixel += pixelSize)
    {
        *destination++ = std::to_integer<std::uint8_t>(source[pixel + FileRed]);
        *destination++ = std::to_integer<std::uint8_t>(source[pixel + FileGreen]);
        *destination++ = std::to_integer<std::uint8_t>(source[pixel + FileBlue]);
        *destination++ = std::to_integer<std::uint8_t>(source[pixel + FileAlpha]);
    }
}
} // namespace

Core::Expected<Texture, DataError> LoadOztTexture(std::span<const std::byte> file)
{
    const std::optional<OztHeader> header = ReadHeader(file);
    if (!header)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    if (header->bitsPerPixel != OztBitsPerPixel || header->width > TextureMaxSide || header->height > TextureMaxSide)
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    Texture texture;
    texture.width = header->width;
    texture.height = header->height;

    texture.textureWidth = TextureSide(texture.width);
    texture.textureHeight = TextureSide(texture.height);
    texture.format = PixelFormat::Rgba;
    const std::size_t pixelSize = BytesPerPixel(texture.format);
    texture.pixels.assign(static_cast<std::size_t>(texture.textureWidth) *
                              static_cast<std::size_t>(texture.textureHeight) * pixelSize,
                          0);
    if (texture.width <= 0 || texture.height <= 0)
    {
        return texture;
    }
    if (file.size() < OztPixelsOffset)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    const std::size_t rowSize = static_cast<std::size_t>(texture.width) * pixelSize;
    const std::span<const std::byte> pixels = file.subspan(OztPixelsOffset);
    if (pixels.size() / rowSize < static_cast<std::size_t>(texture.height))
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    for (int row = 0; row < texture.height; ++row)
    {
        CopyRow(pixels.subspan(static_cast<std::size_t>(row) * rowSize, rowSize), texture, row);
    }
    return texture;
}
} // namespace Client::Data::Textures
