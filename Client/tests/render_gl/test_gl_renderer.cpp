#include "Render/Gl/GlRenderer.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Data/Textures/Texture.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/SpriteVertex.h"
#include "support/GlDrawTest.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::Gl::GlTexture;
using Client::Render::Gl::Rgba8;
using Client::Tests::FramebufferSide;
using Client::Tests::TestQuad;

using GlRendererTest = Client::Tests::GlDrawTest;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 White{255, 255, 255, 255};

Texture OneTexel(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
{
    return Texture{.width = 1,
                   .height = 1,
                   .textureWidth = 1,
                   .textureHeight = 1,
                   .format = PixelFormat::Rgba,
                   .pixels = {red, green, blue, alpha}};
}

// The rectangle from y = 32 to 64 is the top half of the screen: with gluOrtho2D y grows upward, and the original flips
// its own (H − y) to draw from the top.
TEST_F(GlRendererTest, TheYGoesUpLikeGluOrtho2D)
{
    const Client::Core::Expected<GlTexture, std::string> white =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 255, 255), {});
    ASSERT_TRUE(white.HasValue()) << white.Error();

    Draw(white.Value(), TestQuad{.bottom = FramebufferSide / 2.0f});

    EXPECT_EQ(Cells(1, 2), (std::vector<std::optional<Rgba8>>{White, Transparent}));
}

// Channel by channel: yellow times (1, 0, 1, 0.6) gives red, and alpha 0.6 × 255 = 153. Without blending, the alpha
// stays as is in the framebuffer.
TEST_F(GlRendererTest, ModulatesTheTextureByTheVertexColor)
{
    const Client::Core::Expected<GlTexture, std::string> yellow =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 0, 255), {});
    ASSERT_TRUE(yellow.HasValue()) << yellow.Error();

    Draw(yellow.Value(), TestQuad{.color = {.red = 1.0f, .green = 0.0f, .blue = 1.0f, .alpha = 0.6f}});

    EXPECT_EQ(Cells(1, 1), (std::vector<std::optional<Rgba8>>{Rgba8{255, 0, 0, 153}}));
}

// Draw's screen is the viewport and the matrix: with a 32 × 32 one, the rectangle covering it fills the bottom-left
// quarter of the 64 × 64 framebuffer, not the whole framebuffer.
TEST_F(GlRendererTest, TheViewportIsTheScreen)
{
    const Client::Core::Expected<GlTexture, std::string> white =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 255, 255), {});
    ASSERT_TRUE(white.HasValue()) << white.Error();
    constexpr int Half = FramebufferSide / 2;
    const TestQuad screen{.right = static_cast<float>(Half), .top = static_cast<float>(Half)};
    const std::array<Client::Render::SpriteVertex, 6> vertices = Client::Tests::Triangles(screen);

    m_renderer->Draw(vertices, white.Value(), Client::Core::PixelSize{Half, Half});
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Transparent, Transparent, White, Transparent}));
}

// Every three vertices are a separate triangle (GL_TRIANGLES), so one Draw holds several non-touching quads: one in the
// bottom-left cell and one in the top-right cell of a 4 × 4 grid, the rest undrawn. A fan or a strip would build
// triangles between the two quads' vertices and fill some of the middle.
TEST_F(GlRendererTest, EachThreeVerticesAreATriangle)
{
    const Client::Core::Expected<GlTexture, std::string> white =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 255, 255), {});
    ASSERT_TRUE(white.HasValue()) << white.Error();
    constexpr float Quarter = FramebufferSide / 4.0f;
    constexpr auto Side = static_cast<float>(FramebufferSide);
    const auto bottomLeft = Client::Tests::Triangles(TestQuad{.right = Quarter, .top = Quarter});
    const auto topRight = Client::Tests::Triangles(
        TestQuad{.left = Side - Quarter, .bottom = Side - Quarter, .right = Side, .top = Side});
    std::vector<Client::Render::SpriteVertex> vertices(bottomLeft.begin(), bottomLeft.end());
    vertices.insert(vertices.end(), topRight.begin(), topRight.end());

    m_renderer->Draw(vertices, white.Value(), m_framebuffer->Size());
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    std::vector<std::optional<Rgba8>> expected(16, Transparent);
    expected[3] = White;  // top row, right
    expected[12] = White; // bottom row, left
    EXPECT_EQ(Cells(4, 4), expected);
}
} // namespace
