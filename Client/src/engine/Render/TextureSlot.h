#pragma once

#include <cstddef>
#include <cstdint>

// A texture's slot number in the original's table: the one traces and the research name (0x15 is Logo\Login_Back01.jpg,
// 06 § El login). MuRet has one table per scene (Gl::TextureTable) instead of a global one, but indexes it by the same
// number, so a scene's drawing compares with the original's trace.
namespace Client::Render
{
struct TextureSlot
{
    std::uint16_t value = 0;

    friend bool operator==(TextureSlot, TextureSlot) = default;
};

// Keeps the ordinal of a mesh whose texture file could not replace a resident slot.
inline constexpr TextureSlot UnboundModelTextureSlot{0xffff};

// The original table's slots: 0x622e40 zeroes it with 0x64e6 dwords, 1845 slots of 0x38 bytes from 0x7d3a198.
inline constexpr std::size_t OriginalTextureSlotCount = 1845;
static_assert(OriginalTextureSlotCount * 0x38 == 0x64e6 * 4, "el memset de 0x622e40");

// MuRet's text slots, after the original's. The original uploads each text to slot 0 (Interface\FontInput.tga, the
// 256 × 32 box) every time it draws it.
// MuRet does not reproduce its cached text (0x466000), which uses slots 10000 + index handed out by 0x466560.
// MuRet's cache (Render/Text/TextCache.h) keeps each box in one of these: with 256, 8 MiB of textures.
inline constexpr std::size_t TextTextureSlotCount = 256;
inline constexpr TextureSlot FirstTextTextureSlot{static_cast<std::uint16_t>(OriginalTextureSlotCount)};

// MuRet's guild marks, after the text slots. The original paints each mark into slot 0x22 (Interface\guild.tga) with
// glTexImage2D right before its quad (0x5f16a0); MuRet builds the frame's batches and draws them later, so each mark
// has its own slot: one per guild table entry (2000, 0x447b50) and background (0x5f16a0's second argument),
// Scenes::GuildMarkTextures.
inline constexpr std::size_t GuildMarkTextureSlotCount = 4000;
inline constexpr TextureSlot FirstGuildMarkTextureSlot{
    static_cast<std::uint16_t>(OriginalTextureSlotCount + TextTextureSlotCount)};
// The mark editor's color squares, after the marks: 0x5f1bb0 paints the whole 0x22 in a palette color (or the
// crossed-out 0) before its quad; MuRet gives each of the 16 colors one.
inline constexpr std::size_t GuildSwatchTextureSlotCount = 16;
inline constexpr TextureSlot FirstGuildSwatchTextureSlot{
    static_cast<std::uint16_t>(OriginalTextureSlotCount + TextTextureSlotCount + GuildMarkTextureSlotCount)};
// The alliance list rows' marks (P-C289), after the colors: 0x46dfa0 paints each row's into entry 1999
// (0x5f16a0(1999, 1)) and draws its quad right away, so in the original each row sees its own; with the frame batch,
// slot (1999, 1) would hold the last one's. MuRet gives each of the list's 6 rows one (+0x104 of 0x45f630,
// Scenes::GuildSmallList).
inline constexpr std::size_t GuildUnionMarkTextureSlotCount = 6;
inline constexpr TextureSlot FirstGuildUnionMarkTextureSlot{static_cast<std::uint16_t>(
    OriginalTextureSlotCount + TextTextureSlotCount + GuildMarkTextureSlotCount + GuildSwatchTextureSlotCount)};
inline constexpr std::size_t TextureSlotCount = OriginalTextureSlotCount + TextTextureSlotCount +
                                                GuildMarkTextureSlotCount + GuildSwatchTextureSlotCount +
                                                GuildUnionMarkTextureSlotCount;
} // namespace Client::Render
