#include "Render/Gl/GlSpriteRenderer.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Core::PixelSize;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::OriginalQuad;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Render::Gl::GlSpriteRenderer;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Tests::FramebufferSide;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Red{255, 0, 0, 255};
constexpr Rgba8 Green{0, 255, 0, 255};
constexpr Rgba8 Blue{0, 0, 255, 255};
constexpr Rgba8 White{255, 255, 255, 255};

// A white texel, a green one and the 2 × 2 background. None is from an original scene.
constexpr TextureSlot WhiteSlot{0x1};
constexpr TextureSlot GreenSlot{0x2};
constexpr TextureSlot BackgroundSlot{0x3};
constexpr TextureSlot MissingSlot{0x2a};

constexpr PixelSize Screen{FramebufferSide, FramebufferSide};
constexpr auto Side = static_cast<float>(FramebufferSide);
constexpr float Half = Side / 2.0f;

// Texels in buffer order: row 0 first.
LoadedTexture Rgba(TextureSlot slot, int width, int height, std::vector<std::uint8_t> pixels)
{
    return {slot.value,
            {},
            Texture{.width = width,
                    .height = height,
                    .textureWidth = width,
                    .textureHeight = height,
                    .format = PixelFormat::Rgba,
                    .pixels = std::move(pixels)}};
}

// A pixel quad without flags: [left, right) × [bottom, top) with y up, which in the original's space (y from the top)
// starts at H − top.
OriginalQuad PixelQuad(TextureSlot slot, float left, float bottom, float right, float top, float alpha = 0.0f)
{
    return OriginalQuad{
        .slot = slot, .x = left, .y = Side - top, .width = right - left, .height = top - bottom, .alpha = alpha};
}

// Whether the cell was drawn: a single color, and not the freshly cleared framebuffer's transparent.
bool Drawn(const std::optional<Rgba8>& cell)
{
    return cell.has_value() && !(*cell == Transparent);
}

// 8-bit blending rounds as the driver likes: each channel within one of the computed value.
void ExpectNear(const std::optional<Rgba8>& cell, Rgba8 expected)
{
    ASSERT_TRUE(cell.has_value());
    // Not named `near`: windows.h defines it as an empty macro.
    const auto withinOne = [](std::uint8_t actual, std::uint8_t wanted) { return std::abs(actual - wanted) <= 1; };
    EXPECT_TRUE(withinOne(cell->red, expected.red) && withinOne(cell->green, expected.green) &&
                withinOne(cell->blue, expected.blue) && withinOne(cell->alpha, expected.alpha))
        << ::testing::PrintToString(*cell) << " contra " << ::testing::PrintToString(expected);
}

class GlSpriteRendererTest : public Client::Tests::GlDrawTest
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
        // The background: red and green in row 0, blue and white in row 1.
        const std::array<LoadedTexture, 3> textures{
            Rgba(WhiteSlot, 1, 1, {255, 255, 255, 255}), Rgba(GreenSlot, 1, 1, {0, 255, 0, 255}),
            Rgba(BackgroundSlot, 2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255})};
        const Client::Core::Expected<void, std::string> loaded = m_textures->Load(textures);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        m_batch.Begin(Screen);
    }

    void DrawBatch()
    {
        m_sprites->Draw(m_batch, *m_textures, m_log);
        ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    std::optional<GlSpriteRenderer> m_sprites;
    std::optional<TextureTable> m_textures;
    SpriteBatch m_batch;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// The login background is a 640 × 480 quad scaled to the screen, with row 0's v on the top edge: the buffer's first row
// ends up at the top, as in the original.
TEST_F(GlSpriteRendererTest, TheFirstRowIsAtTheTop)
{
    m_batch.Quad(
        OriginalQuad{.slot = BackgroundSlot, .width = 640, .height = 480, .scaleSize = true, .scalePosition = true});

    DrawBatch();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Red, Green, Blue, White}));
}

// The viewport and the matrix are the batch's screen, W × H and not H × W: with a 64 × 32 one, the scaled 640 × 480
// quad fills the bottom half of the framebuffer, like 0x6219b0's viewport (0, 0, W, H) and gluOrtho2D(0, W, 0, H). A
// square screen would not show them swapped.
TEST_F(GlSpriteRendererTest, TheScreenIsWidthByHeight)
{
    m_batch.Begin(PixelSize{FramebufferSide, FramebufferSide / 2});
    m_batch.Quad(
        OriginalQuad{.slot = WhiteSlot, .width = 640, .height = 480, .scaleSize = true, .scalePosition = true});

    DrawBatch();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Transparent, Transparent, White, White}));
}

// 0x620a90's additive adds (GL_ONE, GL_ONE) ignoring alpha: green at 0.6 over red gives full yellow. 0x620a00's normal
// blend mixes by alpha (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA): blue at 0.6 over red gives (0.4 · 255, 0, 0.6 · 255),
// and alpha 0.6 · 0.6 + 0.4 · 1 = 0.76 of 255.
TEST_F(GlSpriteRendererTest, AdditiveAddsAndAlphaBlends)
{
    m_batch.SetColor(Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f});
    m_batch.Quad(PixelQuad(WhiteSlot, 0, 0, Side, Side));
    m_batch.SetBlend(BlendMode::Additive);
    m_batch.SetColor(Color{.red = 0.0f, .green = 1.0f, .blue = 0.0f, .alpha = 0.6f});
    m_batch.Quad(PixelQuad(WhiteSlot, 0, 0, Half, Side));
    m_batch.SetBlend(BlendMode::Alpha);
    m_batch.SetColor(Color{.red = 0.0f, .green = 0.0f, .blue = 1.0f, .alpha = 0.6f});
    m_batch.Quad(PixelQuad(WhiteSlot, Half, 0, Side, Side));

    DrawBatch();

    const std::vector<std::optional<Rgba8>> cells = Cells(2, 1);
    EXPECT_EQ(cells[0], (Rgba8{255, 255, 0, 255}));
    ExpectNear(cells[1], Rgba8{102, 0, 153, 194});
}

// glAlphaFunc(GL_GREATER, 0.25): with the normal blend, the quad with alpha 0.25 is not drawn and the one with 0.26 is.
// The additive turns the alpha test off and draws both.
TEST_F(GlSpriteRendererTest, TheAlphaTestDropsAQuarter)
{
    m_batch.Quad(PixelQuad(WhiteSlot, 0, Half, Half, Side, 0.25f));
    m_batch.Quad(PixelQuad(WhiteSlot, Half, Half, Side, Side, 0.26f));
    m_batch.SetBlend(BlendMode::Additive);
    m_batch.Quad(PixelQuad(WhiteSlot, 0, 0, Half, Half, 0.25f));
    m_batch.Quad(PixelQuad(WhiteSlot, Half, 0, Side, Half, 0.26f));

    DrawBatch();

    const std::vector<std::optional<Rgba8>> cells = Cells(2, 2);
    EXPECT_EQ(cells[0], Transparent);
    EXPECT_TRUE(Drawn(cells[1])) << ::testing::PrintToString(cells[1]);
    EXPECT_TRUE(Drawn(cells[2])) << ::testing::PrintToString(cells[2]);
    EXPECT_TRUE(Drawn(cells[3])) << ::testing::PrintToString(cells[3]);
}

// 0x621aa0's untextured quad is only the color, without the alpha test: red at alpha 0.2 over the left half blends with
// the green below even though the green texture stays bound and 0.2 fails the alpha test. Sampling the texture would
// give (0, 0, 0, 0.2) and no red would show.
TEST_F(GlSpriteRendererTest, AColorQuadHasNoTextureNorAlphaTest)
{
    m_batch.Quad(PixelQuad(GreenSlot, 0, 0, Side, Side));
    m_batch.SetColor(Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f, .alpha = 0.2f});
    m_batch.ColorQuad(0, 0, 320, 480);

    DrawBatch();

    const std::vector<std::optional<Rgba8>> cells = Cells(2, 1);
    ExpectNear(cells[0], Rgba8{51, 204, 0, 214});
    EXPECT_EQ(cells[1], Green);
}

// A slot missing from the table is a warning, with the slot in hex as the research names it, and its range is not
// drawn; the following ones are.
TEST_F(GlSpriteRendererTest, AMissingSlotIsSkippedWithAWarning)
{
    m_batch.Quad(PixelQuad(MissingSlot, 0, 0, Half, Side));
    m_batch.Quad(PixelQuad(WhiteSlot, Half, 0, Side, Side));

    DrawBatch();

    EXPECT_EQ(Cells(2, 1), (std::vector<std::optional<Rgba8>>{Transparent, White}));
    ASSERT_EQ(m_sink.lines.size(), 1u);
    EXPECT_EQ(m_sink.levels[0], Client::Core::LogLevel::Warning);
    EXPECT_NE(m_sink.lines[0].find("0x2a"), std::string::npos) << m_sink.lines[0];
}

TEST_F(GlSpriteRendererTest, AMissingSlotIsWarnedOnce)
{
    m_batch.Quad(PixelQuad(MissingSlot, 0, 0, Half, Side));

    DrawBatch();
    m_batch.Begin(Screen);
    m_batch.Quad(PixelQuad(MissingSlot, 0, 0, Half, Side));
    DrawBatch();

    EXPECT_EQ(m_sink.lines.size(), 1u);
}
} // namespace
