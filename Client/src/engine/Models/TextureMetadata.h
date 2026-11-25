#pragma once

#include "Data/Textures/Texture.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>

namespace Client::Scenes
{
// Research05 § Texturas: +20/+24 son los lados del buffer y +28 sus componentes. Se copian
// del Texture realmente decodificado; esta metadata CPU no describe el estado ni el packing GL.
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
} // namespace Client::Scenes
