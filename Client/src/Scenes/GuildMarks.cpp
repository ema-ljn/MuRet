#include "Scenes/GuildMarks.h"

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Scenes/GuildState.h"
#include "Scenes/TextureSets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x5f16e2..0x5f17c6: los 16 DWORD de la paleta, 0xAABBGGRR. El 0 lo arma 0x5f16d0..0x5f170f: (fondo ? 0 : 0x80) << 24.
constexpr std::array<std::uint32_t, 16> Palette{
    0x00000000u, 0xff000000u, 0xff808080u, 0xffffffffu, 0xff0000ffu, 0xff0080ffu, 0xff00ffffu, 0xff00ff80u,
    0xff00ff00u, 0xff80ff00u, 0xffffff00u, 0xffff8000u, 0xffff0000u, 0xffff0080u, 0xffff00ffu, 0xff8000ffu,
};
constexpr std::uint32_t HalfAlpha = 0x80u;
constexpr std::uint32_t AlphaShift = 24u;
constexpr std::size_t Channels = 4;
constexpr std::uint32_t ByteMask = 0xffu;

// 0x5f1c66 y 0x5f1c88..0x5f1ca6: el negro del 0 y sus diagonales grises.
constexpr std::uint32_t SwatchBlack = 0xff000000u;
constexpr std::uint32_t SwatchGray = 0xff808080u;

static_assert(2 * GuildTableSize == Render::GuildMarkTextureSlotCount, "un slot por entrada y por fondo");
static_assert(Palette.size() == Render::GuildSwatchTextureSlotCount, "un slot por color");

void PutThePixel(GuildMarkImage& image, std::size_t pixel, std::uint32_t color)
{
    for (std::size_t channel = 0; channel < Channels; ++channel)
        image[pixel * Channels + channel] = static_cast<std::uint8_t>((color >> (8u * channel)) & ByteMask);
}
static_assert(GuildMarkSide * GuildMarkSide == GuildMarkNibbles, "los 64 nibbles llenan el 8 × 8");
} // namespace

Core::Expected<GuildMarkImage, std::string> PaintTheGuildMark(const GuildState& guild, std::int32_t index,
                                                              bool clearBackground)
{
    if (index < 0 || static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(
            std::format("0x5f17de: la marca del índice {} cae fuera de la tabla de los gremios (0x787aa48)", index));
    const GuildTableEntry& entry = guild.table[static_cast<std::size_t>(index)];
    GuildMarkImage image{};
    for (std::size_t pixel = 0; pixel < GuildMarkNibbles; ++pixel)
    {
        const std::uint8_t nibble = entry.mark[pixel];
        if (nibble >= Palette.size())
            return Core::Unexpected(std::format("0x5f17f6: el nibble {} de la marca {} lee después de la paleta "
                                                "(0x77f3d84)",
                                                nibble, index));
        const std::uint32_t color = nibble == 0 ? (clearBackground ? 0u : HalfAlpha) << AlphaShift : Palette[nibble];
        PutThePixel(image, pixel, color);
    }
    return image;
}

Core::Expected<GuildMarkImage, std::string> PaintTheSwatch(std::int32_t color)
{
    if (color < 0 || static_cast<std::size_t>(color) >= Palette.size())
        return Core::Unexpected(
            std::format("0x5f1c4b: el color {} del editor de la marca lee fuera de la paleta (0x77f3d84)", color));
    GuildMarkImage image{};
    const std::uint32_t fill = color == 0 ? SwatchBlack : Palette[static_cast<std::size_t>(color)];
    for (std::size_t pixel = 0; pixel < GuildMarkNibbles; ++pixel)
        PutThePixel(image, pixel, fill);
    if (color != 0)
        return image;
    for (std::size_t step = 0; step < GuildMarkSide; ++step)
    {
        PutThePixel(image, step * (GuildMarkSide + 1), SwatchGray);
        PutThePixel(image, (step + 1) * (GuildMarkSide - 1), SwatchGray);
    }
    return image;
}

GuildMarkTextures::GuildMarkTextures(Render::ITextureStore& store) : m_store(&store) {}

GuildMarkTextures::~GuildMarkTextures()
{
    for (const auto& [slot, image] : m_uploaded)
        m_store->Release(Render::TextureSlot{slot});
}

Render::TextureSlot GuildMarkTextures::SlotOf(std::int32_t index, bool clearBackground)
{
    return Render::TextureSlot{
        static_cast<std::uint16_t>(Render::FirstGuildMarkTextureSlot.value + index * 2 + (clearBackground ? 1 : 0))};
}

Core::Expected<Render::TextureSlot, std::string> GuildMarkTextures::Upload(const GuildState& guild, std::int32_t index,
                                                                           bool clearBackground)
{
    auto painted = PaintTheGuildMark(guild, index, clearBackground);
    if (!painted.HasValue())
        return Core::Unexpected(std::move(painted).Error());
    const Render::TextureSlot slot = SlotOf(index, clearBackground);
    if (auto loaded = Load(slot, painted.Value()); !loaded.HasValue())
        return Core::Unexpected(std::format("0x5f183d: la marca {} no sube: {}", index, loaded.Error()));
    return slot;
}

Core::Expected<Render::TextureSlot, std::string> GuildMarkTextures::UploadSwatch(std::int32_t color)
{
    auto painted = PaintTheSwatch(color);
    if (!painted.HasValue())
        return Core::Unexpected(std::move(painted).Error());
    const Render::TextureSlot slot{static_cast<std::uint16_t>(Render::FirstGuildSwatchTextureSlot.value + color)};
    if (auto loaded = Load(slot, painted.Value()); !loaded.HasValue())
        return Core::Unexpected(std::format("0x5f1cff: el color {} no sube: {}", color, loaded.Error()));
    return slot;
}

Core::Expected<void, std::string> GuildMarkTextures::Load(Render::TextureSlot slot, const GuildMarkImage& image)
{
    const auto known = m_uploaded.find(slot.value);
    if (known != m_uploaded.end() && known->second == image)
        return {};
    constexpr auto Side = static_cast<int>(GuildMarkSide);
    const Data::Textures::LoadedTexture texture{
        .slot = slot.value,
        .sampling = TextureSetsDetail::NearestClamp,
        .texture = Data::Textures::Texture{.width = Side,
                                           .height = Side,
                                           .textureWidth = Side,
                                           .textureHeight = Side,
                                           .format = Data::Textures::PixelFormat::Rgba,
                                           .pixels = {image.begin(), image.end()}},
    };
    if (auto loaded = m_store->Load(std::span(&texture, 1)); !loaded.HasValue())
        return Core::Unexpected(std::move(loaded).Error());
    m_uploaded.insert_or_assign(slot.value, image);
    return {};
}
} // namespace Client::Scenes
