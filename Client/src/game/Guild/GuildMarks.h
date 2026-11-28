#pragma once

#include "Core/Expected.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// A guild's mark as a texture (CL9 milestone 8, phase 2; 06 § El gremio en pantalla): 0x5f16a0(index, background)
// paints the 64 nibbles of the table's entry `index` (0x787aa48 + 0x16) with its palette into the texture buffer of
// slot 0x22 (Interface\guild.tga, which 0x61b33c uploads with 0x63bd00 in GL_NEAREST and GL_CLAMP) and uploads it again
// with glTexImage2D; its caller draws a quad with slot 0x22 right away. MuRet draws the batches after building them:
// each mark goes to its own slot (Render::FirstGuildMarkTextureSlot) and the quad uses that one.
namespace Client::Guild
{
struct GuildState;

// Interface\guild.tga is 8 × 8 and 32 bits (the OZT header, *static*): 0x5f16a0 walks its height and width (0x7d3a928
// and 0x7d3a92c in float, with _ftol) and uploads with its components' internal format (4) in GL_RGBA.
inline constexpr std::size_t GuildMarkSide = 8;
inline constexpr std::size_t GuildMarkBytes = GuildMarkSide * GuildMarkSide * 4;
using GuildMarkImage = std::array<std::uint8_t, GuildMarkBytes>;

// 0x5f16a0's pixels: the palette at 0x77f3d84..0x77f3dc0 (each DWORD 0xAABBGGRR, in memory R, G, B and A), with 0
// black and alpha 0x80, or 0 with `clearBackground` (the second argument other than 0); the nibbles in order, row by
// row. It stops with an index outside the table (0x787aa5e + index · 0x58 would read other memory) or with a nibble
// above 15 (it would read past the palette); those the 66 writes (0x447b90) go from 0 to 15.
[[nodiscard]] Core::Expected<GuildMarkImage, std::string> PaintTheGuildMark(const GuildState& guild, std::int32_t index,
                                                                            bool clearBackground);

// 0x5f1bb0(x, y, w, h, color), a color's square in the mark editor: fills the 0x22's buffer (the 8 × 8 it walks with
// the texture's height and width) with the palette color (0x77f3d84, the one the last 0x5f16a0 left) or, with 0,
// black (0xff000000) with the two gray diagonals (0xff808080: 0x5f1c83..0x5f1ca6, in steps of 9 and 7 pixels from 0
// and 7). It stops with a color above 15, which would read past the palette.
[[nodiscard]] Core::Expected<GuildMarkImage, std::string> PaintTheSwatch(std::int32_t color);

// 0x5f16a0 for the drawers: paints the mark and gives the slot where it landed, for the quad that follows.
class IGuildMarks
{
public:
    virtual ~IGuildMarks() = default;

    [[nodiscard]] virtual Core::Expected<Render::TextureSlot, std::string>
    Upload(const GuildState& guild, std::int32_t index, bool clearBackground) = 0;
    // 0x5f1bb0: paints the color's square and gives its slot (Render::FirstGuildSwatchTextureSlot + color).
    [[nodiscard]] virtual Core::Expected<Render::TextureSlot, std::string> UploadSwatch(std::int32_t color) = 0;
    // 0x5f16a0 from row `row` of the union list (0x46e09f): paints the mark like Upload, but uploads it to the row's
    // slot (Render::FirstGuildUnionMarkTextureSlot + row), so each row of the batch sees its own (P-C289). It stops
    // with a row outside the 6.
    [[nodiscard]] virtual Core::Expected<Render::TextureSlot, std::string>
    UploadToTheUnionRow(const GuildState& guild, std::int32_t index, bool clearBackground, std::int32_t row) = 0;
};

// The marks in the scene's table: the slot of (index, background) in Render::FirstGuildMarkTextureSlot, in GL_NEAREST
// and GL_CLAMP like 0x22. It uploads only if the pixels changed since the last time (the original uploads the same
// image on every call: what is seen is the same). It releases its slots when it dies. It lives as long as the table.
class GuildMarkTextures final : public IGuildMarks
{
public:
    explicit GuildMarkTextures(Render::ITextureStore& store);
    ~GuildMarkTextures() override;

    GuildMarkTextures(const GuildMarkTextures&) = delete;
    GuildMarkTextures& operator=(const GuildMarkTextures&) = delete;
    GuildMarkTextures(GuildMarkTextures&&) = delete;
    GuildMarkTextures& operator=(GuildMarkTextures&&) = delete;

    [[nodiscard]] Core::Expected<Render::TextureSlot, std::string> Upload(const GuildState& guild, std::int32_t index,
                                                                          bool clearBackground) override;
    [[nodiscard]] Core::Expected<Render::TextureSlot, std::string> UploadSwatch(std::int32_t color) override;
    [[nodiscard]] Core::Expected<Render::TextureSlot, std::string>
    UploadToTheUnionRow(const GuildState& guild, std::int32_t index, bool clearBackground, std::int32_t row) override;

    [[nodiscard]] static Render::TextureSlot SlotOf(std::int32_t index, bool clearBackground);

private:
    // Uploads `image` to the slot if it changed since the last time.
    [[nodiscard]] Core::Expected<void, std::string> Load(Render::TextureSlot slot, const GuildMarkImage& image);

    Render::ITextureStore* m_store;
    std::map<std::uint16_t, GuildMarkImage> m_uploaded;
};
} // namespace Client::Guild
