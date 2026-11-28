#include "Guild/GuildMarks.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Guild/GuildState.h"

#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Guild::PaintTheGuildMark and GuildMarkTextures (CL9 milestone 8, phase 2): 0x5f16a0's pixels with its palette and
// the upload of each mark to its slot.
namespace
{
using Client::Guild::GuildMarkTextures;
using Client::Guild::GuildState;

std::array<std::uint8_t, 4> PixelAt(const Client::Guild::GuildMarkImage& image, std::size_t pixel)
{
    return {image[pixel * 4], image[pixel * 4 + 1], image[pixel * 4 + 2], image[pixel * 4 + 3]};
}

// 0x5f16e2..0x5f17c6: 0 black with alpha 0x80, or clear with the second argument; the others, each DWORD 0xAABBGGRR
// in memory as R, G, B and A. The nibbles in order, row by row.
TEST(GuildMarksTest, ThePaletteIsTheOneOfTheOriginal)
{
    auto guild = std::make_unique<GuildState>();
    auto& mark = guild->table[5].mark;
    for (std::size_t i = 0; i < mark.size(); ++i)
        mark[i] = static_cast<std::uint8_t>(i % 16);
    const auto half = Client::Guild::PaintTheGuildMark(*guild, 5, false);
    const auto clear = Client::Guild::PaintTheGuildMark(*guild, 5, true);
    ASSERT_TRUE(half.HasValue());
    ASSERT_TRUE(clear.HasValue());
    EXPECT_EQ(PixelAt(half.Value(), 0), (std::array<std::uint8_t, 4>{0, 0, 0, 0x80}));
    EXPECT_EQ(PixelAt(clear.Value(), 0), (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
    const std::array<std::array<std::uint8_t, 4>, 16> expected{{
        {0, 0, 0, 0},
        {0, 0, 0, 0xff},
        {0x80, 0x80, 0x80, 0xff},
        {0xff, 0xff, 0xff, 0xff},
        {0xff, 0, 0, 0xff},
        {0xff, 0x80, 0, 0xff},
        {0xff, 0xff, 0, 0xff},
        {0x80, 0xff, 0, 0xff},
        {0, 0xff, 0, 0xff},
        {0, 0xff, 0x80, 0xff},
        {0, 0xff, 0xff, 0xff},
        {0, 0x80, 0xff, 0xff},
        {0, 0, 0xff, 0xff},
        {0x80, 0, 0xff, 0xff},
        {0xff, 0, 0xff, 0xff},
        {0xff, 0, 0x80, 0xff},
    }};
    for (std::size_t pixel = 0; pixel < 64; ++pixel)
    {
        SCOPED_TRACE(pixel);
        EXPECT_EQ(PixelAt(clear.Value(), pixel), expected[pixel % 16]);
    }
}

// 0x5f17de: an index outside the table would read other memory; a nibble of 16 or more, past the palette.
TEST(GuildMarksTest, OutsideTheTableOrThePaletteIsCut)
{
    auto guild = std::make_unique<GuildState>();
    EXPECT_FALSE(Client::Guild::PaintTheGuildMark(*guild, -1, true).HasValue());
    EXPECT_FALSE(Client::Guild::PaintTheGuildMark(*guild, 2000, true).HasValue());
    EXPECT_TRUE(Client::Guild::PaintTheGuildMark(*guild, 1999, true).HasValue());
    guild->table[1].mark[63] = 16;
    EXPECT_FALSE(Client::Guild::PaintTheGuildMark(*guild, 1, true).HasValue());
}

// Each (index, background) in its slot, 8 × 8 RGBA with GL_NEAREST and GL_CLAMP; the same image is not uploaded again,
// a changed one is. When it dies it releases its slots.
TEST(GuildMarksTest, EachMarkGoesToItsSlotAndIsReleased)
{
    auto guild = std::make_unique<GuildState>();
    guild->table[3].mark.fill(4);
    Client::Tests::FakeTextureStore store;
    {
        GuildMarkTextures marks(store);
        const auto slot = marks.Upload(*guild, 3, true);
        ASSERT_TRUE(slot.HasValue());
        EXPECT_EQ(slot.Value().value, Client::Render::FirstGuildMarkTextureSlot.value + 7);
        const std::optional<Client::Data::Textures::LoadedTexture> loaded = store.Find(slot.Value());
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->texture.width, 8);
        EXPECT_EQ(loaded->texture.height, 8);
        EXPECT_EQ(loaded->texture.format, Client::Data::Textures::PixelFormat::Rgba);
        ASSERT_EQ(loaded->texture.pixels.size(), 256u);
        EXPECT_EQ(loaded->texture.pixels[0], 0xff);
        EXPECT_EQ(loaded->texture.pixels[1], 0);
        EXPECT_EQ(loaded->sampling.filter, Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(loaded->sampling.wrap, Client::Data::Textures::Wrap::Clamp);

        ASSERT_TRUE(marks.Upload(*guild, 3, true).HasValue());
        EXPECT_EQ(store.loadCalls, 1);
        guild->table[3].mark[0] = 8;
        ASSERT_TRUE(marks.Upload(*guild, 3, true).HasValue());
        EXPECT_EQ(store.loadCalls, 2);
        const auto other = marks.Upload(*guild, 3, false);
        ASSERT_TRUE(other.HasValue());
        EXPECT_EQ(other.Value().value, Client::Render::FirstGuildMarkTextureSlot.value + 6);
        EXPECT_FALSE(marks.Upload(*guild, 2000, true).HasValue());
    }
    EXPECT_EQ(store.Size(), 0u);
    EXPECT_EQ(store.released.size(), 2u);
}

// The fake table that does not upload: the error names the mark.
TEST(GuildMarksTest, AFailedUploadIsCut)
{
    auto guild = std::make_unique<GuildState>();
    Client::Tests::FakeTextureStore store;
    GuildMarkTextures marks(store);
    store.failNextLoad = true;
    const auto slot = marks.Upload(*guild, 0, true);
    ASSERT_FALSE(slot.HasValue());
    EXPECT_NE(slot.Error().find("0x5f183d"), std::string::npos);
    ASSERT_TRUE(marks.Upload(*guild, 0, true).HasValue());
    EXPECT_EQ(store.loadCalls, 2);
}

// P-C289: each union row's mark (0x5f16a0(1999, 1) from 0x46e09f) goes to its row's slot, after the colors, with the
// entry's pixels; two rows with different marks each keep their own. A row outside the list's 6, or an index outside
// the table, stops.
TEST(GuildMarksTest, EachUnionRowHasItsOwnSlot)
{
    auto guild = std::make_unique<GuildState>();
    Client::Tests::FakeTextureStore store;
    GuildMarkTextures marks(store);
    auto& creating = guild->table[static_cast<std::size_t>(Client::Guild::CreatingGuildIndex)].mark;
    creating.fill(4);
    const auto first = marks.UploadToTheUnionRow(*guild, Client::Guild::CreatingGuildIndex, true, 0);
    creating.fill(8);
    const auto last = marks.UploadToTheUnionRow(*guild, Client::Guild::CreatingGuildIndex, true, 5);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    ASSERT_TRUE(last.HasValue()) << last.Error();
    EXPECT_EQ(first.Value().value, Client::Render::FirstGuildUnionMarkTextureSlot.value);
    EXPECT_EQ(last.Value().value, Client::Render::FirstGuildUnionMarkTextureSlot.value + 5);
    EXPECT_EQ(Client::Render::FirstGuildUnionMarkTextureSlot.value + 6, Client::Render::TextureSlotCount);
    const auto red = store.Find(first.Value());
    const auto green = store.Find(last.Value());
    ASSERT_TRUE(red.has_value());
    ASSERT_TRUE(green.has_value());
    EXPECT_EQ(red->texture.pixels[0], 0xff);
    EXPECT_EQ(red->texture.pixels[1], 0);
    EXPECT_EQ(green->texture.pixels[0], 0);
    EXPECT_EQ(green->texture.pixels[1], 0xff);
    EXPECT_EQ(red->sampling.filter, Client::Data::Textures::Filter::Nearest);

    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, Client::Guild::CreatingGuildIndex, true, 6).HasValue());
    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, Client::Guild::CreatingGuildIndex, true, -1).HasValue());
    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, 2000, true, 0).HasValue());
    EXPECT_EQ(store.loadCalls, 2);
}
} // namespace
