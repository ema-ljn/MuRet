#include "Data/Terrain/TerrainLight.h"

#include "Data/Textures/JpegImage.h"
#include "Data/Textures/OzjTexture.h"

#include <cstdint>

namespace Client::Data::Terrain
{

namespace
{

// El tamaño que acepta MuRet: el de la tabla del original.
constexpr int TerrainLightSide = static_cast<int>(TerrainSide);

// Si `image` es una luz que entra justa en la tabla: 256 × 256 y en color.
bool FitsTheTable(const Textures::JpegImage& image)
{
    return image.width == TerrainLightSide && image.height == TerrainLightSide &&
           image.components == static_cast<int>(TerrainLightChannels);
}

// El color del píxel x de la fila `row` de la imagen: cada byte por TerrainLightScale.
TerrainLightColor ColorOf(const Textures::JpegImage& image, std::size_t x, std::size_t row)
{
    const std::size_t pixel = (row * TerrainSide + x) * TerrainLightChannels;
    TerrainLightColor color{};
    for (std::size_t channel = 0; channel < TerrainLightChannels; ++channel)
    {
        color[channel] = static_cast<float>(image.pixels[pixel + channel]) * TerrainLightScale;
    }
    return color;
}

} // namespace

Core::Expected<std::unique_ptr<TerrainLight>, DataError> LoadTerrainLight(std::span<const std::byte> file)
{
    if (file.size() <= Textures::OzjPrefixSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const auto decoded = Textures::DecodeJpeg(file.subspan(Textures::OzjPrefixSize), TerrainLightSide);
    if (!decoded.HasValue())
    {
        return Core::Unexpected(decoded.Error());
    }
    const Textures::JpegImage& image = decoded.Value();
    if (!FitsTheTable(image))
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    // La fila y de la tabla es la TerrainSide - 1 - y de la imagen (0063b490 §1).
    auto light = std::make_unique<TerrainLight>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            const auto cell = CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y));
            light->cells[cell] = ColorOf(image, x, TerrainSide - 1 - y);
        }
    }
    return light;
}

} // namespace Client::Data::Terrain
