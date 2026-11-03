#include "Render/Text/TextCache.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
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

// Client::Render::Text::TextCache con la tabla de verdad (CL5.3): las cajas suben a la
// TextureTable en los slots del texto, como la caja de 256 × 32 RGBA que 0x554000 sube al slot 0, y
// GlSpriteRenderer las dibuja con la fila 0 arriba.
namespace
{
using Client::App::PixelSize;
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
// En el formato del original, 0xAABBGGRR: el texto rojo sobre un fondo verde, los dos opacos.
constexpr TextStyle RedOnGreen{FontKind::Normal, TextColors{0xff0000ff, 0xff00ff00}};

// Con el framebuffer y el contexto de GlDrawTest: la tabla, el renderer y un lote del tamaño del
// framebuffer.
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

    // El color del píxel (x, y) del framebuffer, con la y desde arriba, como el original.
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

// Cada caja es una textura de 256 × 32 en el slot que le toca, desde el primero del texto, y la
// caché la suelta de la tabla al vaciarse.
TEST_F(TextCacheGlTest, UploadsTheBoxOf256By32)
{
    const FakeGlyphRasterizer rasterizer;
    TextCache cache(rasterizer, *m_textures);
    const TextureSlot second{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value + 1)};

    // 50 letras de 6 columnas: 300, dos cajas.
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

// La fila 0 de la caja va arriba del quad: el texto en (0, 0) deja la tinta de cada letra (el
// bloque de 3 × 4 del rasterizador falso) en la esquina de arriba a la izquierda de su celda, el
// fondo en el resto de las 32 filas, y nada debajo.
TEST_F(TextCacheGlTest, TheTextIsAtTheTopLeftOfTheBox)
{
    // Dos letras de 32 columnas y una celda de 32 filas: la caja llena la mitad de arriba.
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
