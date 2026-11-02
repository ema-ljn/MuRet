#include "Data/Textures/OzjTexture.h"

#include "Data/Textures/JpegImage.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Client::Data::Textures
{
Core::Expected<Texture, DataError> LoadOzjTexture(std::span<const std::byte> file)
{
    if (file.size() <= OzjPrefixSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    auto decoded = DecodeJpeg(file.subspan(OzjPrefixSize), TextureMaxSide);
    if (!decoded.HasValue())
    {
        return Core::Unexpected(decoded.Error());
    }
    const JpegImage image = std::move(decoded).Value();

    Texture texture;
    texture.width = image.width;
    texture.height = image.height;
    texture.textureWidth = TextureSide(image.width);
    texture.textureHeight = TextureSide(image.height);
    texture.format = PixelFormat::Rgb;
    const std::size_t textureRowSize = static_cast<std::size_t>(texture.textureWidth) * BytesPerPixel(texture.format);
    const std::size_t imageRowSize = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.components);
    if (imageRowSize > textureRowSize)
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    texture.pixels.assign(textureRowSize * static_cast<std::size_t>(texture.textureHeight), 0);
    for (std::size_t row = 0; row < static_cast<std::size_t>(image.height); ++row)
    {
        const auto source = image.pixels.begin() + static_cast<std::ptrdiff_t>(row * imageRowSize);
        std::copy(source, source + static_cast<std::ptrdiff_t>(imageRowSize),
                  texture.pixels.begin() + static_cast<std::ptrdiff_t>(row * textureRowSize));
    }
    return texture;
}
} // namespace Client::Data::Textures
