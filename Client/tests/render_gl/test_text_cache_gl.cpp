#include "Render/Text/TextCache.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/TextureTable.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The boxes go to the real TextureTable in the text slots, like the 256 × 32 RGBA box 0x554000 uploads to slot 0, and
// GlSpriteRenderer draws them with row 0 at the top.
namespace
{
using Client::Core::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Render::Gl::GlSpriteRenderer;
using Client::Render::Gl::GlTexture;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextCache;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FramebufferSide;
using Client::Tests::PixelIndex;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Red{255, 0, 0, 255};
constexpr Rgba8 Green{0, 255, 0, 255};
// In the original's 0xAABBGGRR format: red text on a green background, both opaque.
constexpr TextStyle RedOnGreen{FontKind::Normal, TextColors{0xff0000ff, 0xff00ff00}};

class TextCacheGlTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        Client::Core::Expected<GlSpriteRenderer, std::string> renderer = GlSpriteRenderer::Create(Gl());
        ASSERT_TRUE(renderer.HasValue()) << renderer.Error();
        m_sprites.emplace(std::move(renderer).Value());
        m_textures.emplace(Gl());
        m_batch.Begin(PixelSize{FramebufferSide, FramebufferSide});
    }

    // The color of framebuffer pixel (x, y), y from the top like the original.
    [[nodiscard]] Rgba8 PixelAt(const std::vector<Rgba8>& pixels, int x, int y) const
    {
        return pixels[PixelIndex(x, FramebufferSide - 1 - y, FramebufferSide)];
    }

    std::optional<GlSpriteRenderer> m_sprites;
    std::optional<TextureTable> m_textures;
    SpriteBatch m_batch;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

TEST_F(TextCacheGlTest, UploadsTheBoxOf256By32)
{
    const FakeGlyphRasterizer rasterizer;
    TextCache cache(rasterizer, *m_textures);
    const TextureSlot second{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value + 1)};

    // 50 letters of 6 columns: 300, two boxes.
    const Client::Core::Expected<void, std::string> drawn = cache.Draw(m_batch, 0, 0, std::string(50, 'x'), RedOnGreen);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    for (const TextureSlot slot : {Client::Render::FirstTextTextureSlot, second})
    {
        const GlTexture* box = m_textures->Find(slot);
        ASSERT_NE(box, nullptr) << slot.value;
        EXPECT_EQ(box->Width(), 256);
        EXPECT_EQ(box->Height(), 32);
    }
    cache.Clear();
    EXPECT_EQ(m_textures->Find(Client::Render::FirstTextTextureSlot), nullptr);
    EXPECT_EQ(m_textures->Find(second), nullptr);
}

// Box row 0 goes at the top of the quad: text at (0, 0) leaves each letter's ink (the fake rasterizer's 3 × 4 block) at
// the top left of its cell, background in the rest of the 32 rows, and nothing below.
TEST_F(TextCacheGlTest, TheTextIsAtTheTopLeftOfTheBox)
{
    // Two 32-column letters and a 32-row cell: the box fills the top half.
    const FakeGlyphRasterizer rasterizer(32, 32);
    TextCache cache(rasterizer, *m_textures);

    const Client::Core::Expected<void, std::string> drawn = cache.Draw(m_batch, 0, 0, "ab", RedOnGreen);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    m_sprites->Draw(m_batch, *m_textures, m_log);
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    const std::vector<Rgba8> pixels = m_framebuffer->ReadPixels();
    EXPECT_EQ(PixelAt(pixels, 0, 0), Red);
    EXPECT_EQ(PixelAt(pixels, 2, 3), Red);
    EXPECT_EQ(PixelAt(pixels, 3, 0), Green);
    EXPECT_EQ(PixelAt(pixels, 0, 4), Green);
    EXPECT_EQ(PixelAt(pixels, 32, 0), Red);
    EXPECT_EQ(PixelAt(pixels, 34, 3), Red);
    EXPECT_EQ(PixelAt(pixels, 63, 31), Green);
    EXPECT_EQ(PixelAt(pixels, 0, 32), Transparent);
    EXPECT_EQ(PixelAt(pixels, 63, 63), Transparent);
    EXPECT_TRUE(m_sink.lines.empty());
}
} // namespace
