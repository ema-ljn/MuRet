#include "Scenes/CastleFlagUpload.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Scenes/CastleFlagImage.h"
#include "Assets/ModelTextureTable.h"
#include "Math/Ftol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
Core::Expected<void, std::string> UploadTheCastleFlag(std::span<const std::uint8_t, 64> mark, ModelTextureTable& table)
{
    const auto metadata = table.Metadata(0x69c);
    if (!metadata || table.Store() == nullptr)
        return Core::Unexpected(std::string("0x5f1890: la bandera 0x69c no tiene buffer ni backend cargados"));
    const auto width = Math::OriginalFtolLow(metadata->size.x);
    const auto height = Math::OriginalFtolLow(metadata->size.y);
    if (!width || !height || metadata->components != 4)
        return Core::Unexpected(std::string("0x5f1890: la bandera no tiene el buffer RGBA del OZT original"));
    auto painted = PaintTheCastleFlag(mark, *width, *height);
    if (!painted.HasValue())
        return Core::Unexpected(std::move(painted).Error());
    Data::Textures::Texture image = std::move(painted).Value();
    std::vector<std::uint8_t> rgb;
    rgb.reserve(image.pixels.size() / 4 * 3);
    for (std::size_t at = 0; at < image.pixels.size(); at += 4)
        rgb.insert(rgb.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(at),
                   image.pixels.begin() + static_cast<std::ptrdiff_t>(at + 3));
    image.pixels = std::move(rgb);
    image.format = Data::Textures::PixelFormat::Rgb;
    const Data::Textures::LoadedTexture texture{
        .slot = 0x69c,
        .sampling = {.filter = Data::Textures::Filter::Linear, .wrap = Data::Textures::Wrap::Clamp},
        .texture = std::move(image)};
    // 0x5f1b41: glTexImage2D usa interno RGB desde el buffer RGBA. La tabla CPU sigue con cuatro componentes,
    // las dimensiones y las referencias anteriores; no se llama al cargador ni se omiten paquetes repetidos.
    return table.Store()->Load(std::span(&texture, 1));
}
} // namespace Client::Scenes
