#include "Scenes/TextCacheDrawer.h"

#include "App/WindowState.h"
#include "Core/Log.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextCache.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

// Client::Scenes::TextCacheDrawer: el ITextDrawer de las escenas sobre el TextCache de la CL5.3.
// El texto va al lote del frame; si no se puede dibujar, el error va al log una vez y el texto no
// sale.
namespace
{
using Client::App::PixelSize;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextCache;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Scenes::TextCacheDrawer;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr PixelSize Screen{640, 480};
constexpr TextStyle Login{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};
// Un texto sintético: los de Text.bmd no van a los tests.
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

// La tabla no sube: el texto no sale, y el error va al log una vez aunque se repita en cada frame.
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

    // Y cuando la tabla sube, sale.
    tested.batch.Begin(Screen);
    tested.drawer.Draw(10, 20, Synthetic, Login);
    EXPECT_EQ(tested.batch.Vertices().size(), 6u);
}
} // namespace
