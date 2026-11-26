#include "Scenes/CastleFlagImage.h"

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Math/Ftol.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Client::Scenes
{
Core::Expected<Data::Textures::Texture, std::string> PaintTheCastleFlag(std::span<const std::uint8_t, 64> mark,
                                                                        int width, int height)
{
    if (width <= 0 || height <= 0 || width > Data::Textures::TextureMaxSide || height > Data::Textures::TextureMaxSide)
        return Core::Unexpected(std::string("0x5f1890: la bandera no tiene un buffer de dimensiones válidas"));
    for (const std::uint8_t color : mark)
        if (color >= 16)
            return Core::Unexpected(std::string("0x5f1a7f: el color cae fuera de la paleta de 16 entradas"));
    constexpr std::array<std::uint32_t, 16> Palette{
        0x00000000u, 0xff000000u, 0xff808080u, 0xffffffffu, 0xff0000ffu, 0xff0080ffu, 0xff00ffffu, 0xff00ff80u,
        0xff00ff00u, 0xff80ff00u, 0xffffff00u, 0xffff8000u, 0xffff0000u, 0xffff0080u, 0xffff00ffu, 0xff8000ffu};
    Data::Textures::Texture image{.width = width,
                                  .height = height,
                                  .textureWidth = width,
                                  .textureHeight = height,
                                  .format = Data::Textures::PixelFormat::Rgba,
                                  .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(width * height) * 4)};
    std::size_t centerCursor = 0;
    std::uint32_t gray = 0;
    for (int y = 0; y < height; ++y, gray += 0x10101u)
    {
        // 0x5f1abb: no se redondea ni trunca el segundo término antes de sumarlo al dword firmado.
        // Estas operaciones con dimensiones de hasta 512 son exactas en double, igual que en x87.
        const auto q = Math::OriginalFtolLow(static_cast<double>(y) * static_cast<double>(1.6f) + 50.0).value();
        const auto base = std::bit_cast<std::int32_t>((static_cast<std::uint32_t>(q) - 65536u) << 8u);
        const auto border =
            static_cast<std::uint32_t>(Math::OriginalFtolLow(static_cast<double>(base) +
                                                             static_cast<double>(y) * static_cast<double>(2.55f) + 50.0)
                                           .value());
        for (int x = 0; x < width; ++x)
        {
            std::uint32_t pixel = 0;
            if (x >= width / 2 - 16 && x < width / 2 + 16 && y >= height / 2 - 16 && y < height / 2 + 16)
            {
                const auto source = (centerCursor / 32 / 4) * 8 + (centerCursor % 32 / 4);
                pixel = Palette[mark[source]];
                ++centerCursor;
            }
            else if (x < 3 || x > width - 4 || std::bit_cast<std::int32_t>(gray) < 0xa0a0a || y > height - 10)
                pixel = border;
            else
                pixel = gray + 0xff000000u;
            const auto at = static_cast<std::size_t>(y * width + x) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel)
                image.pixels[at + channel] = static_cast<std::uint8_t>(pixel >> (8u * channel));
        }
    }
    return image;
}
} // namespace Client::Scenes
