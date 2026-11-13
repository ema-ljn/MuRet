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

static_assert(2 * GuildTableSize == Render::GuildMarkTextureSlotCount, "un slot por entrada y por fondo");
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
        for (std::size_t channel = 0; channel < Channels; ++channel)
            image[pixel * Channels + channel] = static_cast<std::uint8_t>((color >> (8u * channel)) & ByteMask);
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
    const auto known = m_uploaded.find(slot.value);
    if (known != m_uploaded.end() && known->second == painted.Value())
        return slot;
    constexpr auto Side = static_cast<int>(GuildMarkSide);
    const Data::Textures::LoadedTexture texture{
        .slot = slot.value,
        .sampling = TextureSetsDetail::NearestClamp,
        .texture = Data::Textures::Texture{.width = Side,
                                           .height = Side,
                                           .textureWidth = Side,
                                           .textureHeight = Side,
                                           .format = Data::Textures::PixelFormat::Rgba,
                                           .pixels = {painted.Value().begin(), painted.Value().end()}},
    };
    if (auto loaded = m_store->Load(std::span(&texture, 1)); !loaded.HasValue())
        return Core::Unexpected(std::format("0x5f183d: la marca {} no sube: {}", index, loaded.Error()));
    m_uploaded.insert_or_assign(slot.value, painted.Value());
    return slot;
}
} // namespace Client::Scenes
