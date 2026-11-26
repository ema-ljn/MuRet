#pragma once

#include "Data/Textures/Texture.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>

namespace Client::Models
{
// Research05 § Texturas: +20/+24 are the buffer's sides and +28 its components. They are copied from the actually
// decoded Texture; this CPU metadata does not describe the GL state or packing.
struct TextureMetadata
{
    explicit TextureMetadata(const Data::Textures::Texture& texture)
        : size(static_cast<float>(texture.textureWidth), static_cast<float>(texture.textureHeight)),
          components(static_cast<std::uint8_t>(Data::Textures::BytesPerPixel(texture.format)))
    {
    }

    glm::vec2 size;
    std::uint8_t components;
};
} // namespace Client::Models
