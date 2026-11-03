#include "Render/Gl/GlSpriteRenderer.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
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

// Client::Render::Gl::GlSpriteRenderer: el SpriteBatch de un frame en GL, con la mezcla de cada
// tramo (0x620a00 y 0x620a90), el alpha test del original como un discard en 0,25, la textura
// apagada del quad sin textura y los slots que la tabla no tiene (06 § Primitivas 2D).
namespace
{
using Client::App::PixelSize;
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

// Los slots de las texturas de prueba: un téxel blanco, uno verde y el fondo de 2 × 2. Ninguno es
// de una escena del original.
constexpr TextureSlot WhiteSlot{0x1};
constexpr TextureSlot GreenSlot{0x2};
constexpr TextureSlot BackgroundSlot{0x3};
// Uno que la tabla no tiene.
constexpr TextureSlot MissingSlot{0x2a};

constexpr PixelSize Screen{FramebufferSide, FramebufferSide};
constexpr auto Side = static_cast<float>(FramebufferSide);
constexpr float Half = Side / 2.0f;

// Una textura RGBA de `width` × `height`, con los téxeles en el orden del buffer: la fila 0
// primero.
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

// Un quad en píxeles, sin banderas: el rectángulo [left, right) × [bottom, top) con la y para
// arriba, que en el espacio del original (la y desde arriba) empieza en H − top.
OriginalQuad PixelQuad(TextureSlot slot, float left, float bottom, float right, float top, float alpha = 0.0f)
{
    return OriginalQuad{
        .slot = slot, .x = left, .y = Side - top, .width = right - left, .height = top - bottom, .alpha = alpha};
}

// Si la celda se dibujó: es de un solo color y no es el transparente del framebuffer recién limpio.
bool Drawn(const std::optional<Rgba8>& cell)
{
    return cell.has_value() && !(*cell == Transparent);
}

// La mezcla en 8 bits redondea como quiere el driver: cada canal, a uno de lo que da la cuenta.
void ExpectNear(const std::optional<Rgba8>& cell, Rgba8 expected)
{
    ASSERT_TRUE(cell.has_value());
    // No se llama `near`: windows.h lo define como una macro vacía.
    const auto withinOne = [](std::uint8_t actual, std::uint8_t wanted) { return std::abs(actual - wanted) <= 1; };
    EXPECT_TRUE(withinOne(cell->red, expected.red) && withinOne(cell->green, expected.green) &&
                withinOne(cell->blue, expected.blue) && withinOne(cell->alpha, expected.alpha))
        << ::testing::PrintToString(*cell) << " contra " << ::testing::PrintToString(expected);
}

// Con el framebuffer y el contexto de GlDrawTest: la tabla con las texturas de prueba, el renderer
// y un log que guarda lo que escribe.
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
        // El fondo: rojo y verde en la fila 0, azul y blanco en la 1.
        const std::array<LoadedTexture, 3> textures{
            Rgba(WhiteSlot, 1, 1, {255, 255, 255, 255}), Rgba(GreenSlot, 1, 1, {0, 255, 0, 255}),
            Rgba(BackgroundSlot, 2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255})};
        const Client::Core::Expected<void, std::string> loaded = m_textures->Load(textures);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        m_batch.Begin(Screen);
    }

    // Dibuja el lote en el framebuffer.
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

// El fondo del login es un quad de 640 × 480 escalado a la pantalla, con la v de la fila 0 en el
// borde de arriba: la primera fila del buffer queda arriba, como en el original.
TEST_F(GlSpriteRendererTest, TheFirstRowIsAtTheTop)
{
    m_batch.Quad(
        OriginalQuad{.slot = BackgroundSlot, .width = 640, .height = 480, .scaleSize = true, .scalePosition = true});

    DrawBatch();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Red, Green, Blue, White}));
}

// El viewport y la matriz son los de la pantalla del lote, W × H y no H × W: con una de 64 × 32, el
// quad de 640 × 480 escalado llena la mitad de abajo del framebuffer, como el viewport (0, 0, W, H)
// y gluOrtho2D(0, W, 0, H) de 0x6219b0. Con una pantalla cuadrada no se vería si se cambian.
TEST_F(GlSpriteRendererTest, TheScreenIsWidthByHeight)
{
    m_batch.Begin(PixelSize{FramebufferSide, FramebufferSide / 2});
    m_batch.Quad(
        OriginalQuad{.slot = WhiteSlot, .width = 640, .height = 480, .scaleSize = true, .scalePosition = true});

    DrawBatch();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>{Transparent, Transparent, White, White}));
}

// La aditiva de 0x620a90 suma (GL_ONE, GL_ONE) sin mirar el alfa: el verde con 0,6 sobre el rojo
// da amarillo entero. La normal de 0x620a00 mezcla por el alfa (GL_SRC_ALPHA,
// GL_ONE_MINUS_SRC_ALPHA): el azul con 0,6 sobre el rojo da (0,4 · 255, 0, 0,6 · 255), y el alfa,
// 0,6 · 0,6 + 0,4 · 1 = 0,76 de 255.
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

// glAlphaFunc(GL_GREATER, 0,25): con la normal, el quad con alfa 0,25 no se dibuja y el de 0,26 sí.
// La aditiva apaga el alpha test, y dibuja los dos.
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

// El quad sin textura de 0x621aa0 es solo el color, sin el alpha test: el rojo con alfa 0,2 sobre
// la mitad de la izquierda se mezcla con el verde de abajo aunque quede atada la textura verde, y
// aunque 0,2 no pase el alpha test. Muestreando la textura daría (0, 0, 0, 0,2) y no se vería rojo.
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

// Un slot que la tabla no tiene es un aviso, con el slot en hexadecimal como lo nombra el research,
// y su tramo no se dibuja; los que siguen, sí.
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

// El lote se dibuja en cada frame: el aviso sale una vez por slot y no en cada frame.
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
