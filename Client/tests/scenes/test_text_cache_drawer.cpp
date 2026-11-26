#include "Render/Frame/TextCacheDrawer.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextCache.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <string>

namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Render::Frame::TextCacheDrawer;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextCache;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr PixelSize Screen{640, 480};
constexpr TextStyle Login{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};
// Synthetic: Text.bmd texts do not go in the tests.
constexpr const char* Synthetic = "texto de prueba";

struct Harness
{
    Harness() : log(sink, LogLevel::Debug), cache(rasterizer, store), drawer(cache, batch, log)
    {
        batch.Begin(Screen);
    }

    FakeGlyphRasterizer rasterizer;
    FakeTextureStore store;
    RecordingSink sink;
    Log log;
    SpriteBatch batch;
    TextCache cache;
    TextCacheDrawer drawer;
};

TEST(TextCacheDrawerTest, DrawsIntoTheBatchOfTheFrame)
{
    Harness tested;

    tested.drawer.Draw(10, 20, Synthetic, Login);

    ASSERT_EQ(tested.batch.Commands().size(), 1u);
    ASSERT_TRUE(tested.batch.Commands()[0].texture.has_value());
    EXPECT_EQ(tested.batch.Commands()[0].texture->value, Client::Render::FirstTextTextureSlot.value);
    EXPECT_EQ(tested.batch.Vertices().size(), 6u);
    EXPECT_TRUE(tested.sink.lines.empty());
}

TEST(TextCacheDrawerTest, MeasuresWithTheRasterizer)
{
    Harness tested;

    const auto extent = tested.drawer.Measure(FontKind::Normal, Synthetic);

    EXPECT_EQ(extent.width, 6 * static_cast<int>(std::string(Synthetic).size()));
    EXPECT_EQ(extent.height, 13);
}

// The table does not upload: the text does not show, and the error goes to the log once even if repeated every frame.
TEST(TextCacheDrawerTest, AnErrorGoesToTheLogOnce)
{
    Harness tested;

    for (int frame = 0; frame < 3; ++frame)
    {
        tested.store.failNextLoad = true;
        tested.batch.Begin(Screen);
        tested.drawer.Draw(10, 20, Synthetic, Login);
        EXPECT_TRUE(tested.batch.Vertices().empty());
    }

    ASSERT_EQ(tested.sink.lines.size(), 1u);
    EXPECT_EQ(tested.sink.levels[0], LogLevel::Error);
    EXPECT_NE(tested.sink.lines[0].find("la tabla falsa no sube"), std::string::npos) << tested.sink.lines[0];
    EXPECT_EQ(tested.sink.lines[0].find(Synthetic), std::string::npos) << "el texto no va al log";

    // And once the table uploads, it shows.
    tested.batch.Begin(Screen);
    tested.drawer.Draw(10, 20, Synthetic, Login);
    EXPECT_EQ(tested.batch.Vertices().size(), 6u);
}

// DrawColumns goes to the cache with its box: the text (90 columns) centered in 300 from x = 10 is two boxes, of 256
// and 44. A box of less than 0 columns (mode 2 with a negative box, 0x4671cf) draws nothing, and its error goes to the
// log once.
TEST(TextCacheDrawerTest, DrawsAWindowOfColumnsInBoxesOf256)
{
    Harness tested;

    tested.drawer.DrawColumns(10, 20, Synthetic, Login, -105, 300);

    EXPECT_EQ(tested.batch.Vertices().size(), 12u);
    EXPECT_EQ(tested.cache.UsedSlots(), 2u);
    EXPECT_TRUE(tested.sink.lines.empty());

    tested.batch.Begin(Screen);
    tested.drawer.DrawColumns(10, 20, Synthetic, Login, 0, -300);
    tested.drawer.DrawColumns(10, 20, Synthetic, Login, 0, -300);
    EXPECT_TRUE(tested.batch.Vertices().empty());
    ASSERT_EQ(tested.sink.lines.size(), 1u);
    EXPECT_NE(tested.sink.lines[0].find("0x4671cf"), std::string::npos) << tested.sink.lines[0];
}
} // namespace
