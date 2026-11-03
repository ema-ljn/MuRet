#include "Render/Gl/GlRenderer.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Platform/GlLoader.h"
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

// Client::Render::Gl::GlRenderer: el 2D del original en el pipeline programable (06 § Primitivas
// 2D): la y para arriba de gluOrtho2D(0, W, 0, H), el viewport de la pantalla entera y la textura
// por el color del vértice, como el GL_MODULATE de la función fija.
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

// Una textura de un téxel RGBA.
Texture OneTexel(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
{
    return Texture{.width = 1,
                   .height = 1,
                   .textureWidth = 1,
                   .textureHeight = 1,
                   .format = PixelFormat::Rgba,
                   .pixels = {red, green, blue, alpha}};
}

// El rectángulo de y = 32 a 64 es la mitad de arriba de la pantalla: con gluOrtho2D, la y crece
// hacia arriba, y el original da vuelta la suya (H − y) para dibujar desde arriba.
TEST_F(GlRendererTest, TheYGoesUpLikeGluOrtho2D)
{
    const Client::Core::Expected<GlTexture, std::string> white =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 255, 255), {});
    ASSERT_TRUE(white.HasValue()) << white.Error();

    Draw(white.Value(), TestQuad{.bottom = FramebufferSide / 2.0f});

    EXPECT_EQ(Cells(1, 2), (std::vector<std::optional<Rgba8>>{White, Transparent}));
}

// La textura por el color del vértice, canal por canal: el amarillo por (1, 0, 1, 0,6) da rojo, y
// el alfa, 0,6 × 255 = 153. Sin mezcla, el alfa queda tal cual en el framebuffer.
TEST_F(GlRendererTest, ModulatesTheTextureByTheVertexColor)
{
    const Client::Core::Expected<GlTexture, std::string> yellow =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 0, 255), {});
    ASSERT_TRUE(yellow.HasValue()) << yellow.Error();

    Draw(yellow.Value(), TestQuad{.color = {.red = 1.0f, .green = 0.0f, .blue = 1.0f, .alpha = 0.6f}});

    EXPECT_EQ(Cells(1, 1), (std::vector<std::optional<Rgba8>>{Rgba8{255, 0, 0, 153}}));
}

// La pantalla que recibe Draw es el viewport y la matriz: con una de 32 × 32, el rectángulo que la
// cubre llena el cuarto de abajo a la izquierda del framebuffer de 64 × 64, no el framebuffer.
TEST_F(GlRendererTest, TheViewportIsTheScreen)
{
    const Client::Core::Expected<GlTexture, std::string> white =
        GlTexture::Upload(Gl(), OneTexel(255, 255, 255, 255), {});
    ASSERT_TRUE(white.HasValue()) << white.Error();
    constexpr int Half = FramebufferSide / 2;
    const TestQuad screen{.right = static_cast<float>(Half), .top = static_cast<float>(Half)};
    const std::array<Client::Render::SpriteVertex, 6> vertices = Client::Tests::Triangles(screen);

    m_renderer->Draw(vertices, white.Value(), Client::App::PixelSize{Half, Half});
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Transparent, Transparent, White, Transparent}));
}

// Cada tres vértices son un triángulo suelto (GL_TRIANGLES), así que en un solo Draw van varios
// quads que no se tocan, como los que juntará el SpriteBatch de la CL5.2: uno en la celda de abajo
// a la izquierda y otro en la de arriba a la derecha de una grilla de 4 × 4, y el resto queda sin
// dibujar. Un abanico o una tira armarían triángulos entre los vértices de los dos y llenarían algo
// del medio.
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
    expected[3] = White;  // la fila de arriba, a la derecha
    expected[12] = White; // la de abajo, a la izquierda
    EXPECT_EQ(Cells(4, 4), expected);
}
} // namespace
