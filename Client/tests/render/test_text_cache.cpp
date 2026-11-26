#include "Render/Text/TextCache.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"

#include "support/FakeGlyphRasterizer.h"
#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// Texts rasterized once and reused (CL5.3), with a cap in slots and the rule of evicting the one drawn longest ago.
// Runs without GL: the table is the fake one, which records what is uploaded and what is released, and the rasterizer
// is the fake one (6 columns per letter, 13 rows, the ink in the 3 × 4 block at the top left of each letter).
namespace
{
using Client::Core::PixelSize;
using Client::Data::Textures::Filter;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Wrap;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::BoxedQuad;
using Client::Render::Text::BoxShape;
using Client::Render::Text::CenteredInBox;
using Client::Render::Text::CenterInBox;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextCache;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextExtent;
using Client::Render::Text::TextPieces;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FakeTextureStore;

constexpr PixelSize Screen{640, 480};
constexpr PixelSize Screen1024{1024, 768};
// The slots of these tests' caches: four from the first text slot.
constexpr TextureSlot First{0x735};
constexpr std::size_t FourSlots = 4;
constexpr TextStyle Login{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};

std::vector<std::uint16_t> SlotsOf(const SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const DrawCommand& command : batch.Commands())
    {
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            slots.push_back(command.texture ? command.texture->value : std::uint16_t{0xffff});
        }
    }
    return slots;
}

bool SameVertex(const SpriteVertex& a, const SpriteVertex& b)
{
    return a.x == b.x && a.y == b.y && a.u == b.u && a.v == b.v && a.color == b.color;
}

class TextCacheTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Screen);
    }

    // Fails the test if the cache returns an error.
    void Draw(TextCache& cache, std::string_view text, const TextStyle& style = Login, int x = 10, int y = 20)
    {
        const auto drawn = cache.Draw(m_batch, x, y, text, style);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    // Mode 2 as the scenes request it: CenterInBox with the cache's rasterizer and DrawColumns.
    Client::Core::Expected<void, std::string> DrawCentered(TextCache& cache, int x, int y, std::string_view text,
                                                           int box)
    {
        const CenteredInBox placed = CenterInBox(m_rasterizer, Login.font, text, box);
        return cache.DrawColumns(m_batch, x, y, text, Login, -placed.textColumn, placed.width);
    }

    FakeGlyphRasterizer m_rasterizer;
    FakeTextureStore m_store;
    SpriteBatch m_batch;
};

// The same text with the same font and the same colors is rasterized and uploaded once, and drawn each time with its
// box.
TEST_F(TextCacheTest, TheSameTextIsRasterizedOnce)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "abc");
    Draw(cache, "abc");

    EXPECT_EQ(m_rasterizer.drawCalls, 1);
    EXPECT_EQ(m_store.loadCalls, 1);
    EXPECT_EQ(m_store.loaded, (std::vector<std::uint16_t>{First.value}));
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value, First.value}));
}

// The key is the text, the font and both colors: changing any of them makes another entry.
TEST_F(TextCacheTest, ADifferentColorIsAnotherEntry)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "abc");
    Draw(cache, "abc", TextStyle{FontKind::Normal, TextColors{0xff0000ff, 0x80000000}});
    Draw(cache, "abc", TextStyle{FontKind::Normal, TextColors{0xffffffff, 0}});
    Draw(cache, "abc", TextStyle{FontKind::Bold, Login.colors});

    EXPECT_EQ(m_rasterizer.drawCalls, 4);
    EXPECT_EQ(cache.Size(), 4u);
    // In another batch: the four of this one are not evicted (ATextOfThisBatchIsNotEvicted).
    m_batch.Begin(Screen);
    Draw(cache, "abd");
    EXPECT_EQ(m_rasterizer.drawCalls, 5);
}

// Another screen width brings other fonts (FontSet.h): the cache empties, releases its slots and rasterizes with the
// new rasterizer.
TEST_F(TextCacheTest, ResizingEmptiesIt)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    Draw(cache, "abc");
    const FakeGlyphRasterizer wider(7, 14);

    cache.ChangeRasterizer(wider);

    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.UsedSlots(), 0u);
    EXPECT_EQ(m_store.released, (std::vector<std::uint16_t>{First.value}));
    EXPECT_EQ(m_store.Size(), 0u);
    Draw(cache, "abc");
    EXPECT_EQ(wider.drawCalls, 1);
    EXPECT_EQ(m_rasterizer.drawCalls, 1);
    EXPECT_EQ(cache.Measure(FontKind::Normal, "abc"), (TextExtent{21, 14}));
}

// With the four slots taken, the new text evicts the one drawn longest ago: A, B, C and D in one batch, A again in the
// next, and E evicts B, not A. Then B comes back and evicts C.
TEST_F(TextCacheTest, KeepsAtMostItsCap)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    for (const std::string_view text : {"A", "B", "C", "D"})
    {
        Draw(cache, text);
    }
    m_batch.Begin(Screen);
    Draw(cache, "A");
    ASSERT_TRUE(m_store.released.empty());

    Draw(cache, "E");

    EXPECT_EQ(cache.Size(), 4u);
    EXPECT_EQ(cache.UsedSlots(), 4u);
    ASSERT_EQ(m_store.released.size(), 1u);
    const std::uint16_t slotOfB = static_cast<std::uint16_t>(First.value + 1);
    EXPECT_EQ(m_store.released[0], slotOfB);
    EXPECT_EQ(m_store.loaded.back(), slotOfB);
    EXPECT_EQ(m_rasterizer.drawCalls, 5);

    Draw(cache, "A");
    EXPECT_EQ(m_rasterizer.drawCalls, 5);
    Draw(cache, "B");
    EXPECT_EQ(m_rasterizer.drawCalls, 6);
    EXPECT_EQ(m_store.released.back(), static_cast<std::uint16_t>(First.value + 2));
}

// The batch stores each quad's slot, and GlSpriteRenderer only looks it up when drawing: a text already in the batch
// is not evicted, because its slot would draw another box. With the four slots holding texts of the batch, the fifth
// is an error and releases nothing; in the next batch, it evicts the oldest.
TEST_F(TextCacheTest, ATextOfThisBatchIsNotEvicted)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    for (const std::string_view text : {"A", "B", "C", "D"})
    {
        Draw(cache, text);
    }

    const auto drawn = cache.Draw(m_batch, 10, 20, "E", Login);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("4 slots"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(m_store.released.empty());
    EXPECT_EQ(cache.Size(), 4u);
    EXPECT_EQ(m_store.loadCalls, 4);
    EXPECT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value, static_cast<std::uint16_t>(First.value + 1),
                                                            static_cast<std::uint16_t>(First.value + 2),
                                                            static_cast<std::uint16_t>(First.value + 3)}));

    m_batch.Begin(Screen);
    Draw(cache, "E");
    EXPECT_EQ(m_store.released, (std::vector<std::uint16_t>{First.value}));
    EXPECT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value}));
}

// A text of more than 256 columns takes one box per 256, and each TextPieces quad goes with its own: the second box is
// the text from column 256.
TEST_F(TextCacheTest, AWideTextUsesABoxPer256Columns)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    const std::string text(50, 'x');

    Draw(cache, text);

    EXPECT_EQ(cache.UsedSlots(), 2u);
    EXPECT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value, static_cast<std::uint16_t>(First.value + 1)}));
    const auto second = m_store.Find(TextureSlot{static_cast<std::uint16_t>(First.value + 1)});
    ASSERT_TRUE(second.has_value());
    // Letter 43 starts at column 258: column 2 of the second box.
    const auto pixelAt = [&](int column)
    {
        const auto offset = static_cast<std::size_t>(column * 4);
        return std::array<std::uint8_t, 4>{second->texture.pixels[offset], second->texture.pixels[offset + 1],
                                           second->texture.pixels[offset + 2], second->texture.pixels[offset + 3]};
    };
    EXPECT_EQ(pixelAt(1), (std::array<std::uint8_t, 4>{0, 0, 0, 0x80}));
    EXPECT_EQ(pixelAt(2), (std::array<std::uint8_t, 4>{0xff, 0xff, 0xff, 0xff}));
}

// One box per 256 columns, rounding up: a text of exactly 256 is one, and one of 257, two.
TEST_F(TextCacheTest, ATextOf256ColumnsIsOneBox)
{
    const FakeGlyphRasterizer eightWide(8, 13);
    TextCache cache(eightWide, m_store, First, FourSlots);

    Draw(cache, std::string(32, 'x'));
    EXPECT_EQ(cache.UsedSlots(), 1u);
    Draw(cache, std::string(33, 'x'));
    EXPECT_EQ(cache.UsedSlots(), 3u);
}

// The DIB of 0x467060 is 1280 columns wide, and 0x465a00 does not check where it ends: the box that starts at column
// 1280 reads the row below (Q-C78). MuRet draws each box from its column, without the DIB: the sixth box has the
// letters from 1280 on.
TEST_F(TextCacheTest, ABoxPastTheColumn1280HasItsLetters)
{
    TextCache cache(m_rasterizer, m_store, First, 8);
    // 240 letras de 6 columnas: 1.440, seis cajas.
    const std::string text(240, 'x');

    const auto drawn = cache.Draw(m_batch, 0, 0, text, Login);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(cache.UsedSlots(), 6u);
    const auto sixth = m_store.Find(TextureSlot{static_cast<std::uint16_t>(First.value + 5)});
    ASSERT_TRUE(sixth.has_value());
    const auto alphaAt = [&](int column) { return sixth->texture.pixels[static_cast<std::size_t>(column * 4) + 3]; };
    // Letter 213 goes from 1278 to 1280 (column 0 of the box), and 214 starts at 1284: column 4. Between the two,
    // background.
    EXPECT_EQ(alphaAt(0), 0xff);
    EXPECT_EQ(alphaAt(1), 0x80);
    EXPECT_EQ(alphaAt(3), 0x80);
    EXPECT_EQ(alphaAt(4), 0xff);
    EXPECT_EQ(alphaAt(6), 0xff);
    // The box has 160 columns of the text: 159 is background, 160 is no longer the text's.
    EXPECT_EQ(alphaAt(159), 0x80);
    EXPECT_EQ(alphaAt(160), 0);
}

// The box is the original's: 256 × 32 RGBA, with GL_NEAREST and GL_CLAMP as 0x63bd00 uploads FontInput.tga in slot 0
// (0x2600 and 0x2900), and the text at the top left in its colors.
TEST_F(TextCacheTest, TheBoxIsTheTextOnItsColors)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "ab", TextStyle{FontKind::Normal, TextColors{0xff3322aa, 0x80000000}});

    const auto box = m_store.Find(First);
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(box->sampling.filter, Filter::Nearest);
    EXPECT_EQ(box->sampling.wrap, Wrap::Clamp);
    EXPECT_EQ(box->texture.width, 256);
    EXPECT_EQ(box->texture.height, 32);
    EXPECT_EQ(box->texture.textureWidth, 256);
    EXPECT_EQ(box->texture.textureHeight, 32);
    EXPECT_EQ(box->texture.format, PixelFormat::Rgba);
    ASSERT_EQ(box->texture.pixels.size(), 256u * 32u * 4u);
    const std::vector<std::uint8_t>& pixels = box->texture.pixels;
    EXPECT_EQ((std::array{pixels[0], pixels[1], pixels[2], pixels[3]}),
              (std::array<std::uint8_t, 4>{0xaa, 0x22, 0x33, 0xff}));
    EXPECT_EQ((std::array{pixels[12], pixels[13], pixels[14], pixels[15]}),
              (std::array<std::uint8_t, 4>{0, 0, 0, 0x80}));
    // Column 12 is no longer the text's, which is 12 wide.
    EXPECT_EQ((std::array{pixels[48], pixels[49], pixels[50], pixels[51]}), (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
}

// The quads are those of TextPieces for (x, y) and the batch's screen, each with its box's slot: with x = 400 at
// 640 × 480, the 300-column text is cut at the edge.
TEST_F(TextCacheTest, TheQuadsAreThoseOfTextPieces)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    const std::string text(50, 'x');

    Draw(cache, text, Login, 400, 300);

    SpriteBatch expected;
    expected.Begin(Screen);
    const std::vector pieces = TextPieces(400, 300, TextExtent{300, 13}, Screen);
    ASSERT_EQ(pieces.size(), 1u);
    for (std::size_t k = 0; k < pieces.size(); ++k)
    {
        auto quad = pieces[k].quad;
        quad.slot = TextureSlot{static_cast<std::uint16_t>(First.value + k)};
        expected.Quad(quad);
    }
    ASSERT_EQ(m_batch.Vertices().size(), expected.Vertices().size());
    for (std::size_t i = 0; i < expected.Vertices().size(); ++i)
    {
        EXPECT_TRUE(SameVertex(m_batch.Vertices()[i], expected.Vertices()[i])) << i;
    }
    EXPECT_EQ(SlotsOf(m_batch), SlotsOf(expected));
}

// An empty text draws nothing, like 0x467060 without a cap, and does not enter the cache.
TEST_F(TextCacheTest, AnEmptyTextDrawsNothing)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "");

    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(m_store.loadCalls, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

// A text that needs more boxes than the cache's slots is an error, and evicts nobody.
TEST_F(TextCacheTest, ATextWiderThanTheCacheIsAnError)
{
    TextCache cache(m_rasterizer, m_store, First, 1);
    Draw(cache, "abc");

    const auto drawn = cache.Draw(m_batch, 0, 0, std::string(50, 'x'), Login);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("2 cajas"), std::string::npos) << drawn.Error();
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_TRUE(m_store.released.empty());
}

// If the table does not upload, the error reaches the caller that draws, and the slots are free again.
TEST_F(TextCacheTest, AStoreThatFailsIsAnError)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    m_store.failNextLoad = true;

    const auto drawn = cache.Draw(m_batch, 0, 0, "abc", Login);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("la tabla falsa no sube"), std::string::npos) << drawn.Error();
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.UsedSlots(), 0u);
    EXPECT_TRUE(m_batch.Vertices().empty());
    Draw(cache, "abc");
    EXPECT_EQ(cache.UsedSlots(), 1u);
}

// On destruction, and with Clear, it releases the slots it uses.
TEST_F(TextCacheTest, ItReleasesItsSlots)
{
    {
        TextCache cache(m_rasterizer, m_store, First, FourSlots);
        Draw(cache, "abc");
        Draw(cache, "def");
        cache.Clear();
        EXPECT_EQ(m_store.Size(), 0u);
        EXPECT_EQ(cache.Size(), 0u);
        Draw(cache, "ghi");
    }

    EXPECT_EQ(m_store.Size(), 0u);
    EXPECT_EQ(m_store.released.size(), 3u);
}
// A bubble line is a one-slot box with its shape in the key: the same one is reused, another leading column is another
// entry. The quad is BoxedQuad's, even if the text is empty; without a width it does not draw.
TEST_F(TextCacheTest, ABubbleLineIsOneBoxWithItsShape)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    const BoxShape shape{.width = 40, .rows = 12, .prefixColumns = 6, .prefixColor = 0xff00ffc8};

    ASSERT_TRUE(cache.DrawBoxed(m_batch, 620, 470, "ab", Login, shape, true).HasValue());
    ASSERT_TRUE(cache.DrawBoxed(m_batch, 620, 470, "ab", Login, shape, true).HasValue());
    EXPECT_EQ(m_rasterizer.drawCalls, 1);
    EXPECT_EQ(cache.Size(), 1u);
    const auto box = m_store.Find(First);
    ASSERT_TRUE(box.has_value());
    const std::vector<std::uint8_t>& pixels = box->texture.pixels;
    EXPECT_EQ((std::array{pixels[0], pixels[1], pixels[2], pixels[3]}),
              (std::array<std::uint8_t, 4>{0xc8, 0xff, 0x00, 0xff}));
    EXPECT_EQ((std::array{pixels[24], pixels[25], pixels[26], pixels[27]}),
              (std::array<std::uint8_t, 4>{0xff, 0xff, 0xff, 0xff}));

    SpriteBatch expected;
    expected.Begin(Screen);
    auto quad = BoxedQuad(620, 470, 40, 12, true, Screen);
    quad.slot = First;
    expected.Quad(quad);
    ASSERT_EQ(m_batch.Vertices().size(), 2 * expected.Vertices().size());
    for (std::size_t i = 0; i < expected.Vertices().size(); ++i)
    {
        EXPECT_TRUE(SameVertex(m_batch.Vertices()[i], expected.Vertices()[i])) << i;
    }

    ASSERT_TRUE(cache
                    .DrawBoxed(m_batch, 0, 0, "ab", Login,
                               BoxShape{.width = 40, .rows = 12, .prefixColumns = 0, .prefixColor = 0xff00ffc8}, true)
                    .HasValue());
    ASSERT_TRUE(cache.DrawBoxed(m_batch, 0, 0, "", Login, shape, true).HasValue());
    EXPECT_EQ(cache.Size(), 3u);
    ASSERT_TRUE(cache.DrawBoxed(m_batch, 0, 0, "ab", Login, BoxShape{}, true).HasValue());
    EXPECT_EQ(cache.Size(), 3u);
    EXPECT_EQ(SlotsOf(m_batch).size(), 4u);
}

// 0x467060 with a width: the box has the requested columns, with the whole background and column `firstColumn` of the
// text at its column 0. Centered (mode 2), "ab" (12 columns) in 20 starts with 4 of background; with the width below cx
// (mode 1), "abcd" in 10 shows from column 14. The same box is reused; without columns it does not draw.
TEST_F(TextCacheTest, AWindowOfColumnsHasTheBackgroundAndTheTextFromItsColumn)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    constexpr std::array<std::uint8_t, 4> Ink{0xff, 0xff, 0xff, 0xff};
    constexpr std::array<std::uint8_t, 4> Background{0, 0, 0, 0x80};
    constexpr std::array<std::uint8_t, 4> Outside{0, 0, 0, 0};
    const auto pixel = [](const std::vector<std::uint8_t>& pixels, std::size_t column)
    { return std::array{pixels[column * 4], pixels[column * 4 + 1], pixels[column * 4 + 2], pixels[column * 4 + 3]}; };

    ASSERT_TRUE(cache.DrawColumns(m_batch, 30, 40, "ab", Login, -4, 20).HasValue());
    ASSERT_TRUE(cache.DrawColumns(m_batch, 30, 40, "ab", Login, -4, 20).HasValue());
    EXPECT_EQ(m_rasterizer.drawCalls, 1);
    const auto centered = m_store.Find(First);
    ASSERT_TRUE(centered.has_value());
    EXPECT_EQ(pixel(centered->texture.pixels, 0), Background);
    EXPECT_EQ(pixel(centered->texture.pixels, 3), Background);
    EXPECT_EQ(pixel(centered->texture.pixels, 4), Ink);
    EXPECT_EQ(pixel(centered->texture.pixels, 10), Ink);
    EXPECT_EQ(pixel(centered->texture.pixels, 16), Background);
    EXPECT_EQ(pixel(centered->texture.pixels, 19), Background);
    EXPECT_EQ(pixel(centered->texture.pixels, 20), Outside);

    ASSERT_TRUE(cache.DrawColumns(m_batch, 30, 60, "abcd", Login, 14, 10).HasValue());
    const auto clipped = m_store.Find(TextureSlot{static_cast<std::uint16_t>(First.value + 1)});
    ASSERT_TRUE(clipped.has_value());
    EXPECT_EQ(pixel(clipped->texture.pixels, 0), Ink);
    EXPECT_EQ(pixel(clipped->texture.pixels, 1), Background);
    EXPECT_EQ(pixel(clipped->texture.pixels, 4), Ink);
    EXPECT_EQ(pixel(clipped->texture.pixels, 9), Background);
    EXPECT_EQ(pixel(clipped->texture.pixels, 10), Outside);

    SpriteBatch expected;
    expected.Begin(Screen);
    for (const auto& [y, extent, slot] :
         {std::tuple{40, TextExtent{20, 13}, First}, std::tuple{40, TextExtent{20, 13}, First},
          std::tuple{60, TextExtent{10, 13}, TextureSlot{static_cast<std::uint16_t>(First.value + 1)}}})
    {
        for (const auto& piece : TextPieces(30, y, extent, Screen))
        {
            auto quad = piece.quad;
            quad.slot = slot;
            expected.Quad(quad);
        }
    }
    ASSERT_EQ(m_batch.Vertices().size(), expected.Vertices().size());
    for (std::size_t i = 0; i < expected.Vertices().size(); ++i)
    {
        EXPECT_TRUE(SameVertex(m_batch.Vertices()[i], expected.Vertices()[i])) << i;
    }

    ASSERT_TRUE(cache.DrawColumns(m_batch, 30, 40, "ab", Login, -4, 0).HasValue());
    ASSERT_TRUE(cache.DrawColumns(m_batch, 30, 40, "", Login, 0, 8).HasValue());
    EXPECT_EQ(cache.Size(), 3u);
    EXPECT_EQ(SlotsOf(m_batch).size(), SlotsOf(expected).size() + 1);
}

// The EDIT keeps its origin in pixels, and the text clip uses the cached buffer of the full text.
TEST_F(TextCacheTest, DrawColumnsAtPixelsKeepsExactOriginAndUsesTheWindowedFullText)
{
    m_batch.Begin(Screen1024);
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto drawn = cache.DrawColumnsAtPixels(m_batch, 239, 21, "abcdefghij", Login, -3, 300);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(cache.Size(), 1u);
    ASSERT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value, First.value + 1}));
    ASSERT_EQ(m_batch.Vertices().size(), 12u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].x, 239.0f);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[6].x, 495.0f);
    EXPECT_EQ(m_batch.Vertices()[0].u, 0.0f);
    EXPECT_EQ(m_batch.Vertices()[6].u, 0.0f);
    EXPECT_EQ(m_rasterizer.drawCalls, 2);
}

TEST_F(TextCacheTest, DrawColumnsAtPixelsKeepsBackgroundForEmptyText)
{
    m_batch.Begin(Screen1024);
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto drawn = cache.DrawColumnsAtPixels(m_batch, 239, 21, "", Login, 0, 8);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(cache.Size(), 1u);
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_FLOAT_EQ(m_batch.Vertices()[0].x, 239.0f);
    const auto texture = m_store.Find(First);
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->texture.pixels[3], 0x80);
    EXPECT_EQ(m_rasterizer.drawCalls, 1);
}

TEST_F(TextCacheTest, DrawColumnsAtPixelsRejectsNegativeWidth)
{
    m_batch.Begin(Screen1024);
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto drawn = cache.DrawColumnsAtPixels(m_batch, 239, 21, "abc", Login, 0, -1);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_EQ(m_store.loadCalls, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

// Mode 2 of 0x467060 (the inventory title): "ab" (12 columns) in a box of 40 goes at column 14, and the whole box is
// background: 14 columns before, the ink of the a at 14, that of the b at 20 and background up to 39. The rows are the
// cell's; what goes past the box stays at 0. The quad is that of TextPieces with the box width, from x.
TEST_F(TextCacheTest, TheModeTwoBoxIsTheBackgroundWithTheTextInTheMiddle)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto drawn = DrawCentered(cache, 10, 20, "ab", 40);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const auto box = m_store.Find(First);
    ASSERT_TRUE(box.has_value());
    const auto alphaAt = [&](int column, int row)
    { return box->texture.pixels[static_cast<std::size_t>((row * 256 + column) * 4) + 3]; };
    EXPECT_EQ(alphaAt(0, 0), 0x80);
    EXPECT_EQ(alphaAt(13, 0), 0x80);
    EXPECT_EQ(alphaAt(14, 0), 0xff);
    EXPECT_EQ(alphaAt(16, 3), 0xff);
    EXPECT_EQ(alphaAt(17, 0), 0x80);
    EXPECT_EQ(alphaAt(20, 0), 0xff);
    EXPECT_EQ(alphaAt(23, 0), 0x80);
    EXPECT_EQ(alphaAt(39, 12), 0x80);
    EXPECT_EQ(alphaAt(40, 0), 0);
    EXPECT_EQ(alphaAt(0, 13), 0);

    SpriteBatch expected;
    expected.Begin(Screen);
    const std::vector pieces = TextPieces(10, 20, TextExtent{40, 13}, Screen);
    ASSERT_EQ(pieces.size(), 1u);
    auto quad = pieces[0].quad;
    quad.slot = First;
    expected.Quad(quad);
    ASSERT_EQ(m_batch.Vertices().size(), expected.Vertices().size());
    for (std::size_t i = 0; i < expected.Vertices().size(); ++i)
    {
        EXPECT_TRUE(SameVertex(m_batch.Vertices()[i], expected.Vertices()[i])) << i;
    }
}

// The title box at 1600 × 1200 is 300 columns: with "ab", (300 − 12) / 2 = 144 and the box ends up at 300, in two
// boxes of 256 and 44. From x = 485 (1212.5 pixels) it does not pass the edge: the quads go at 1212 and 1468. The ink
// of the a is at column 144 of the first; the second is all background.
TEST_F(TextCacheTest, AModeTwoBoxPast256ColumnsGoesInBoxesOf256)
{
    constexpr PixelSize Screen1600{1600, 1200};
    m_batch.Begin(Screen1600);
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto drawn = DrawCentered(cache, 485, 12, "ab", 300);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(cache.UsedSlots(), 2u);
    const std::vector pieces = TextPieces(485, 12, TextExtent{300, 13}, Screen1600);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].quad.x, 1212.0f);
    EXPECT_EQ(pieces[1].quad.x, 1468.0f);
    EXPECT_EQ(pieces[1].width, 44);
    SpriteBatch expected;
    expected.Begin(Screen1600);
    for (std::size_t k = 0; k < pieces.size(); ++k)
    {
        auto quad = pieces[k].quad;
        quad.slot = TextureSlot{static_cast<std::uint16_t>(First.value + k)};
        expected.Quad(quad);
    }
    ASSERT_EQ(m_batch.Vertices().size(), expected.Vertices().size());
    for (std::size_t i = 0; i < expected.Vertices().size(); ++i)
    {
        EXPECT_TRUE(SameVertex(m_batch.Vertices()[i], expected.Vertices()[i])) << i;
    }
    const auto first = m_store.Find(First);
    const auto second = m_store.Find(TextureSlot{static_cast<std::uint16_t>(First.value + 1)});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const auto alphaAt = [](const std::vector<std::uint8_t>& pixels, int column)
    { return pixels[static_cast<std::size_t>(column * 4) + 3]; };
    EXPECT_EQ(alphaAt(first->texture.pixels, 143), 0x80);
    EXPECT_EQ(alphaAt(first->texture.pixels, 144), 0xff);
    EXPECT_EQ(alphaAt(first->texture.pixels, 255), 0x80);
    EXPECT_EQ(alphaAt(second->texture.pixels, 0), 0x80);
    EXPECT_EQ(alphaAt(second->texture.pixels, 43), 0x80);
    EXPECT_EQ(alphaAt(second->texture.pixels, 44), 0);
}

// Past 256, the box is cut at the right edge like a wide text (0x46721f): at 640 × 480, from x = 400, 240 columns
// remain, in a single quad.
TEST_F(TextCacheTest, AWideModeTwoBoxIsCutAtTheRightEdge)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    ASSERT_TRUE(DrawCentered(cache, 400, 12, "ab", 300).HasValue());

    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].x, 400.0f);
    EXPECT_EQ(m_batch.Vertices()[2].x, 640.0f);
}

// A text wider than its box goes at a negative column and TextOutA clips the left: "abcdefgh" (48) in 30 goes at −9
// and the box ends up at 30. The a and the b fall outside, the c at column 3 and the g at 27; the h, past the box.
TEST_F(TextCacheTest, AModeTwoTextWiderThanItsBoxIsCutOnBothSides)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    ASSERT_TRUE(DrawCentered(cache, 10, 20, "abcdefgh", 30).HasValue());

    const auto box = m_store.Find(First);
    ASSERT_TRUE(box.has_value());
    const auto alphaAt = [&](int column) { return box->texture.pixels[static_cast<std::size_t>(column * 4) + 3]; };
    EXPECT_EQ(alphaAt(0), 0x80);
    EXPECT_EQ(alphaAt(2), 0x80);
    EXPECT_EQ(alphaAt(3), 0xff);
    EXPECT_EQ(alphaAt(29), 0xff);
    EXPECT_EQ(alphaAt(30), 0);
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[2].x - m_batch.Vertices()[0].x, 30.0f);
}

// An empty text with a box measures the "0" of 0x6c7ae4 (6 columns): (20 − 6) / 2 = 7, and the box of 20 is all
// background. One that starts with a newline does not reach TextOutA (0x4671e1): background too. Without a box, an
// empty one does not draw.
TEST_F(TextCacheTest, AnEmptyModeTwoTextIsItsBackground)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    ASSERT_TRUE(DrawCentered(cache, 10, 20, "", 20).HasValue());
    ASSERT_TRUE(DrawCentered(cache, 10, 40, "\nab", 40).HasValue());

    EXPECT_EQ(cache.Size(), 2u);
    for (const TextureSlot slot : {First, TextureSlot{static_cast<std::uint16_t>(First.value + 1)}})
    {
        const auto box = m_store.Find(slot);
        ASSERT_TRUE(box.has_value());
        for (int column = 0; column < 20; ++column)
        {
            EXPECT_EQ(box->texture.pixels[static_cast<std::size_t>(column * 4) + 3], 0x80) << column;
        }
    }
    ASSERT_EQ(m_batch.Vertices().size(), 12u);
    EXPECT_EQ(m_batch.Vertices()[2].x - m_batch.Vertices()[0].x, 20.0f);

    ASSERT_TRUE(DrawCentered(cache, 10, 20, "", 0).HasValue());
    EXPECT_EQ(cache.Size(), 2u);
    EXPECT_EQ(m_batch.Vertices().size(), 12u);
}

// The key is the resulting box, not the requested one: 40 and 41 give the same one with "ab" (column 14 and 40
// columns), and 42 another. Draw is another entry even if the text is the same.
TEST_F(TextCacheTest, TheModeTwoBoxIsInTheKey)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "ab");
    ASSERT_TRUE(DrawCentered(cache, 0, 0, "ab", 40).HasValue());
    ASSERT_TRUE(DrawCentered(cache, 0, 0, "ab", 41).HasValue());
    EXPECT_EQ(cache.Size(), 2u);
    ASSERT_TRUE(DrawCentered(cache, 0, 0, "ab", 42).HasValue());
    EXPECT_EQ(cache.Size(), 3u);
    EXPECT_EQ(m_rasterizer.drawCalls, 3);
}

// A box that gives fewer than 0 columns (a negative box) is an error with the address of the rep stos that would clear
// outside the DIB; one that gives 0 draws nothing.
TEST_F(TextCacheTest, ANegativeModeTwoBoxIsAnError)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    const auto negative = DrawCentered(cache, 0, 0, "a", -10);
    ASSERT_FALSE(negative.HasValue());
    EXPECT_NE(negative.Error().find("0x4671cf"), std::string::npos) << negative.Error();

    ASSERT_TRUE(DrawCentered(cache, 0, 0, "ab", 0).HasValue());
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_TRUE(m_batch.Vertices().empty());
}
} // namespace
