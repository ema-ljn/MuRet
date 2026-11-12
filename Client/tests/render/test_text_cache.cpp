#include "Render/Text/TextCache.h"

#include "App/WindowState.h"
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
#include <vector>

// Client::Render::Text::TextCache: los textos rasterizados una vez y reusados (CL5.3), con un tope
// en slots y la regla de sacar el que hace más tiempo que no se dibuja. Corre sin GL: la tabla es
// la falsa, que anota lo que se sube y lo que se suelta, y el rasterizador, el falso (6 columnas
// por letra, 13 filas, la tinta en el bloque de 3 × 4 de arriba a la izquierda de cada letra).
namespace
{
using Client::App::PixelSize;
using Client::Data::Textures::Filter;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Wrap;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::BoxedQuad;
using Client::Render::Text::BoxShape;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextCache;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextExtent;
using Client::Render::Text::TextPieces;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FakeTextureStore;

constexpr PixelSize Screen{640, 480};
// Los slots de las cachés de estos tests: cuatro desde el primero del texto.
constexpr TextureSlot First{0x735};
constexpr std::size_t FourSlots = 4;
constexpr TextStyle Login{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};

// Los slots de los tramos del lote, en orden.
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

    // Dibuja `text` y falla el test si la caché devuelve un error.
    void Draw(TextCache& cache, std::string_view text, const TextStyle& style = Login, int x = 10, int y = 20)
    {
        const auto drawn = cache.Draw(m_batch, x, y, text, style);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    }

    FakeGlyphRasterizer m_rasterizer;
    FakeTextureStore m_store;
    SpriteBatch m_batch;
};

// El mismo texto con la misma fuente y los mismos colores se rasteriza y se sube una vez, y se
// dibuja cada vez con su caja.
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

// La clave es el texto, la fuente y los dos colores: cambiar cualquiera es otra entrada.
TEST_F(TextCacheTest, ADifferentColorIsAnotherEntry)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "abc");
    Draw(cache, "abc", TextStyle{FontKind::Normal, TextColors{0xff0000ff, 0x80000000}});
    Draw(cache, "abc", TextStyle{FontKind::Normal, TextColors{0xffffffff, 0}});
    Draw(cache, "abc", TextStyle{FontKind::Bold, Login.colors});

    EXPECT_EQ(m_rasterizer.drawCalls, 4);
    EXPECT_EQ(cache.Size(), 4u);
    // En otro lote: los cuatro de este no salen (ATextOfThisBatchIsNotEvicted).
    m_batch.Begin(Screen);
    Draw(cache, "abd");
    EXPECT_EQ(m_rasterizer.drawCalls, 5);
}

// Otro ancho de pantalla trae otras fuentes (FontSet.h): la caché se vacía, suelta sus slots y
// rasteriza con el rasterizador nuevo.
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

// Con los cuatro slots ocupados, el texto nuevo saca al que hace más tiempo que no se dibuja: A,
// B, C y D en un lote, A otra vez en el siguiente, y E saca a B, no a A. Después B vuelve y saca a
// C.
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

// El lote guarda el slot de cada quad, y GlSpriteRenderer lo busca recién al dibujarlo: un texto
// que ya está en el lote no sale, porque su slot dibujaría otra caja. Con los cuatro slots en
// textos del lote, el quinto es un error y no suelta nada; en el lote siguiente, saca al más viejo.
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

// Un texto de más de 256 columnas ocupa una caja por cada 256, y cada quad de TextPieces va con la
// suya: la segunda caja es el texto desde la columna 256.
TEST_F(TextCacheTest, AWideTextUsesABoxPer256Columns)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);
    const std::string text(50, 'x');

    Draw(cache, text);

    EXPECT_EQ(cache.UsedSlots(), 2u);
    EXPECT_EQ(SlotsOf(m_batch), (std::vector<std::uint16_t>{First.value, static_cast<std::uint16_t>(First.value + 1)}));
    const auto second = m_store.Find(TextureSlot{static_cast<std::uint16_t>(First.value + 1)});
    ASSERT_TRUE(second.has_value());
    // La letra 43 empieza en la columna 258: la 2 de la segunda caja.
    const auto pixelAt = [&](int column)
    {
        const auto offset = static_cast<std::size_t>(column * 4);
        return std::array<std::uint8_t, 4>{second->texture.pixels[offset], second->texture.pixels[offset + 1],
                                           second->texture.pixels[offset + 2], second->texture.pixels[offset + 3]};
    };
    EXPECT_EQ(pixelAt(1), (std::array<std::uint8_t, 4>{0, 0, 0, 0x80}));
    EXPECT_EQ(pixelAt(2), (std::array<std::uint8_t, 4>{0xff, 0xff, 0xff, 0xff}));
}

// Una caja por cada 256 columnas, redondeando para arriba: un texto de 256 justas es una, y uno de
// 257, dos.
TEST_F(TextCacheTest, ATextOf256ColumnsIsOneBox)
{
    const FakeGlyphRasterizer eightWide(8, 13);
    TextCache cache(eightWide, m_store, First, FourSlots);

    Draw(cache, std::string(32, 'x'));
    EXPECT_EQ(cache.UsedSlots(), 1u);
    Draw(cache, std::string(33, 'x'));
    EXPECT_EQ(cache.UsedSlots(), 3u);
}

// El DIB de 0x467060 mide 1.280 columnas, y 0x465a00 no mira dónde termina: la caja que empieza en
// la columna 1.280 lee la fila de abajo (Q-C78). MuRet dibuja cada caja desde su columna, sin el
// DIB: la sexta caja tiene las letras que van desde la 1.280.
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
    // La letra 213 va de la 1.278 a la 1.280 (la columna 0 de la caja), y la 214 empieza en la
    // 1.284: la 4. Entre las dos, fondo.
    EXPECT_EQ(alphaAt(0), 0xff);
    EXPECT_EQ(alphaAt(1), 0x80);
    EXPECT_EQ(alphaAt(3), 0x80);
    EXPECT_EQ(alphaAt(4), 0xff);
    EXPECT_EQ(alphaAt(6), 0xff);
    // La caja tiene 160 columnas del texto: la 159 es fondo, la 160 ya no es del texto.
    EXPECT_EQ(alphaAt(159), 0x80);
    EXPECT_EQ(alphaAt(160), 0);
}

// La caja es la del original: 256 × 32 en RGBA, con GL_NEAREST y GL_CLAMP como sube 0x63bd00 la
// FontInput.tga del slot 0 (0x2600 y 0x2900), y el texto arriba a la izquierda en sus colores.
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
    // La columna 12 ya no es del texto, que mide 12.
    EXPECT_EQ((std::array{pixels[48], pixels[49], pixels[50], pixels[51]}), (std::array<std::uint8_t, 4>{0, 0, 0, 0}));
}

// Los quads son los de TextPieces para (x, y) y la pantalla del lote, cada uno con el slot de su
// caja: con x = 400 en 640 × 480, el texto de 300 columnas se corta en el borde.
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

// Un texto vacío no dibuja nada, como 0x467060 sin tope, y no entra a la caché.
TEST_F(TextCacheTest, AnEmptyTextDrawsNothing)
{
    TextCache cache(m_rasterizer, m_store, First, FourSlots);

    Draw(cache, "");

    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(m_store.loadCalls, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

// Un texto que pide más cajas que los slots de la caché es un error, y no saca a nadie.
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

// Si la tabla no sube, el error llega al que dibuja, y los slots vuelven a estar libres.
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

// Al morir, y con Clear, suelta los slots que usa.
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
// Un renglón de un globo es una caja de un slot con su forma en la clave: la misma se reusa, otra columna de adelante
// es otra entrada. El quad es el de BoxedQuad, aunque el texto esté vacío; sin ancho no dibuja.
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
} // namespace
