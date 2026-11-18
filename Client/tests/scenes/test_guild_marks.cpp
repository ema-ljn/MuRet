#include "Scenes/GuildMarks.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/GuildState.h"

#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// Scenes::PaintTheGuildMark y GuildMarkTextures (CL9 hito 8, fase 2): los píxeles de 0x5f16a0 con su paleta y la
// subida de cada marca a su slot.
namespace
{
using Client::Scenes::GuildMarkTextures;
using Client::Scenes::GuildState;

std::array<std::uint8_t, 4> PixelAt(const Client::Scenes::GuildMarkImage& image, std::size_t pixel)
{
    return {image[pixel * 4], image[pixel * 4 + 1], image[pixel * 4 + 2], image[pixel * 4 + 3]};
}

// 0x5f16e2..0x5f17c6: el 0 negro con alfa 0x80, o transparente con el segundo argumento; los demás, cada DWORD
// 0xAABBGGRR en memoria como R, G, B y A. Los nibbles en orden, fila por fila.
TEST(GuildMarksTest, ThePaletteIsTheOneOfTheOriginal)
{
    auto guild = std::make_unique<GuildState>();
    auto& mark = guild->table[5].mark;
    for (std::size_t i = 0; i < mark.size(); ++i)
        mark[i] = static_cast<std::uint8_t>(i % 16);
    const auto half = Client::Scenes::PaintTheGuildMark(*guild, 5, false);
    const auto clear = Client::Scenes::PaintTheGuildMark(*guild, 5, true);
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

// 0x5f17de: un índice fuera de la tabla leería otra memoria; un nibble de 16 o más, después de la paleta.
TEST(GuildMarksTest, OutsideTheTableOrThePaletteIsCut)
{
    auto guild = std::make_unique<GuildState>();
    EXPECT_FALSE(Client::Scenes::PaintTheGuildMark(*guild, -1, true).HasValue());
    EXPECT_FALSE(Client::Scenes::PaintTheGuildMark(*guild, 2000, true).HasValue());
    EXPECT_TRUE(Client::Scenes::PaintTheGuildMark(*guild, 1999, true).HasValue());
    guild->table[1].mark[63] = 16;
    EXPECT_FALSE(Client::Scenes::PaintTheGuildMark(*guild, 1, true).HasValue());
}

// Cada (índice, fondo) en su slot, de 8 × 8 en RGBA con GL_NEAREST y GL_CLAMP; la misma imagen no sube otra vez, una
// cambiada sí. Al morir suelta sus slots.
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

// La tabla falsa que no sube: el error dice la marca.
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

// P-C289: la marca de cada renglón de la alianza (0x5f16a0(1999, 1) desde 0x46e09f) va al slot de su renglón, después
// de los colores, con los píxeles de la entrada; dos renglones con marcas distintas quedan cada uno con la suya. Un
// renglón fuera de los 6 de la lista, o un índice fuera de la tabla, corta.
TEST(GuildMarksTest, EachUnionRowHasItsOwnSlot)
{
    auto guild = std::make_unique<GuildState>();
    Client::Tests::FakeTextureStore store;
    GuildMarkTextures marks(store);
    auto& creating = guild->table[static_cast<std::size_t>(Client::Scenes::CreatingGuildIndex)].mark;
    creating.fill(4);
    const auto first = marks.UploadToTheUnionRow(*guild, Client::Scenes::CreatingGuildIndex, true, 0);
    creating.fill(8);
    const auto last = marks.UploadToTheUnionRow(*guild, Client::Scenes::CreatingGuildIndex, true, 5);
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

    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, Client::Scenes::CreatingGuildIndex, true, 6).HasValue());
    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, Client::Scenes::CreatingGuildIndex, true, -1).HasValue());
    EXPECT_FALSE(marks.UploadToTheUnionRow(*guild, 2000, true, 0).HasValue());
    EXPECT_EQ(store.loadCalls, 2);
}
} // namespace
