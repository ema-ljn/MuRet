#include "Render/Frame/SceneText.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::Frame::RasterizerFactory;
using Client::Render::Frame::SceneText;
using Client::Render::Text::FontKind;
using Client::Render::Text::IGlyphRasterizer;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FakeTextureStore;
using Client::Tests::RecordingSink;

constexpr TextStyle Login{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};
constexpr const char* Synthetic = "abc";

// A fake rasterizer whose letter measures the screen width divided by 100, so a test sees which width it was opened
// with. The widths in `failing` do not open.
struct Fonts
{
    RasterizerFactory Factory()
    {
        return [this](int screenWidth) -> Client::Core::Expected<std::unique_ptr<IGlyphRasterizer>, std::string>
        {
            opened.push_back(screenWidth);
            for (const int width : failing)
            {
                if (width == screenWidth)
                {
                    return Client::Core::Unexpected(std::string("la fuente falsa no abre"));
                }
            }
            return std::unique_ptr<IGlyphRasterizer>(std::make_unique<FakeGlyphRasterizer>(screenWidth / 100));
        };
    }

    std::vector<int> opened;
    std::vector<int> failing;
};

struct Harness
{
    Harness() : log(sink, LogLevel::Debug) {}

    std::unique_ptr<SceneText> Make(int width)
    {
        auto made = SceneText::Create(store, batch, fonts.Factory(), width, log);
        EXPECT_TRUE(made.HasValue()) << made.Error();
        return made.HasValue() ? std::move(made).Value() : nullptr;
    }

    FakeTextureStore store;
    SpriteBatch batch;
    Fonts fonts;
    RecordingSink sink;
    Log log;
};

TEST(SceneTextTest, OpensTheFontsForTheScreenWidth)
{
    Harness tested;

    const auto text = tested.Make(640);

    ASSERT_NE(text, nullptr);
    EXPECT_EQ(tested.fonts.opened, std::vector<int>{640});
    EXPECT_EQ(text->Drawer().Measure(FontKind::Normal, Synthetic).width, 3 * 6);
}

// Without the first width's fonts there is no text: the error stops it, as in main.cpp.
TEST(SceneTextTest, FontsThatDoNotOpenFailTheCreate)
{
    Harness tested;
    tested.fonts.failing = {640};

    const auto made = SceneText::Create(tested.store, tested.batch, tested.fonts.Factory(), 640, tested.log);

    ASSERT_FALSE(made.HasValue());
    EXPECT_NE(made.Error().find("la fuente falsa no abre"), std::string::npos) << made.Error();
}

TEST(SceneTextTest, TheSameWidthKeepsTheFonts)
{
    Harness tested;
    const auto text = tested.Make(640);
    ASSERT_NE(text, nullptr);

    text->FollowScreenWidth(640);
    text->FollowScreenWidth(640);

    EXPECT_EQ(tested.fonts.opened, std::vector<int>{640});
}

// Another width: other fonts, and what the cache uploaded with the old ones is released.
TEST(SceneTextTest, AnotherWidthOpensOtherFonts)
{
    Harness tested;
    const auto text = tested.Make(640);
    ASSERT_NE(text, nullptr);
    tested.batch.Begin(PixelSize{640, 480});
    text->Drawer().Draw(0, 0, Synthetic, Login);
    ASSERT_EQ(tested.store.Size(), 1u);

    text->FollowScreenWidth(1280);
    text->FollowScreenWidth(1280);

    EXPECT_EQ(tested.fonts.opened, (std::vector<int>{640, 1280}));
    EXPECT_EQ(text->Drawer().Measure(FontKind::Normal, Synthetic).width, 3 * 12);
    EXPECT_EQ(tested.store.Size(), 0u);

    // And back, the first width's fonts again.
    text->FollowScreenWidth(640);
    EXPECT_EQ(tested.fonts.opened, (std::vector<int>{640, 1280, 640}));
}

// If the new width's fonts do not open, the text keeps the old ones, and the error goes to the log once per width even
// if requested every frame.
TEST(SceneTextTest, FontsThatDoNotOpenKeepTheOldOnes)
{
    Harness tested;
    const auto text = tested.Make(640);
    ASSERT_NE(text, nullptr);
    tested.fonts.failing = {1280};

    for (int frame = 0; frame < 3; ++frame)
    {
        text->FollowScreenWidth(1280);
    }

    EXPECT_EQ(text->Drawer().Measure(FontKind::Normal, Synthetic).width, 3 * 6);
    ASSERT_EQ(tested.sink.lines.size(), 1u);
    EXPECT_EQ(tested.sink.levels[0], LogLevel::Error);
    EXPECT_NE(tested.sink.lines[0].find("la fuente falsa no abre"), std::string::npos) << tested.sink.lines[0];
    EXPECT_EQ(tested.fonts.opened, (std::vector<int>{640, 1280}));

    // With another width that does open, it changes.
    text->FollowScreenWidth(800);
    EXPECT_EQ(text->Drawer().Measure(FontKind::Normal, Synthetic).width, 3 * 8);

    // And the one that did not open is tried again if it comes back after another, with its warning: it is not marked
    // forever.
    text->FollowScreenWidth(1280);
    EXPECT_EQ(tested.fonts.opened, (std::vector<int>{640, 1280, 800, 1280}));
    EXPECT_EQ(tested.sink.lines.size(), 2u);
    EXPECT_EQ(text->Drawer().Measure(FontKind::Normal, Synthetic).width, 3 * 8);
}
} // namespace
