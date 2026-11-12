#include "UI/SlideBanner.h"

#include "App/WindowState.h"
#include "Data/Text/SlideTable.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

// El cartel de Slide.bmd (CL8 hito 3): las colas de [0x5763528] (0x46b720, 0x46abf0, 0x46add0, 0x46a9c0), el sorteo
// de 0x46b680 y el dibujo (0x46b090, 0x46a540, 0x46a850, 0x46a910). Los textos son inventados; el medidor de
// RecordingTextDrawer da 6 columnas por letra y 13 filas.
namespace
{
using Client::App::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordingTextDrawer;
using Client::UI::SlideBanner;
using Client::UI::SlideDrawContext;
using Client::UI::SlideQueue;
using Client::UI::SlideStepContext;

constexpr PixelSize Screen640{640, 480};
// 640 / 6 · 1: los espacios que pone 0x46a9c0 delante de un texto con 6 columnas por espacio.
constexpr std::size_t SpacesAt640 = 106;

class SlideBannerTest : public ::testing::Test
{
protected:
    SlideBannerTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::UI::ResetTheSlideQueues(m_banner, 0);
    }

    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    FontKind m_dcFont = FontKind::Normal;
    TextColors m_colors{};
    SlideBanner m_banner;
    std::int32_t m_mouseX = 700;
    std::int32_t m_mouseY = 0;

    [[nodiscard]] bool Step(std::uint32_t now)
    {
        return Client::UI::StepTheSlides(
                   m_banner, SlideStepContext{.measure = &m_text, .dcFont = m_dcFont, .screen = Screen640, .now = now})
            .HasValue();
    }

    [[nodiscard]] bool Draw(bool slidesOn = true)
    {
        return Client::UI::DrawTheSlides(m_banner, SlideDrawContext{.sprites = m_sprites,
                                                                    .text = m_text,
                                                                    .dcFont = m_dcFont,
                                                                    .textColors = m_colors,
                                                                    .mouseX = m_mouseX,
                                                                    .mouseY = m_mouseY,
                                                                    .slidesOn = slidesOn})
            .HasValue();
    }
};

// 0x46b010: las dos colas en (0, 0x1ad), en el segundo 10, la normal con la fuente normal y la gruesa con la otra.
TEST_F(SlideBannerTest, TheQueuesStartEmptyAtTheBottom)
{
    EXPECT_EQ(m_banner.normal.y, 0x1ad);
    EXPECT_EQ(m_banner.bold.y, 0x1ad);
    EXPECT_EQ(m_banner.normal.seconds, 10U);
    EXPECT_EQ(m_banner.normal.font, FontKind::Normal);
    EXPECT_EQ(m_banner.bold.font, FontKind::Bold);
    EXPECT_TRUE(m_banner.normal.text.empty());
}

// 0x46b720: de 0 a 2 a la normal y de 3 a 5 a la gruesa, con el tipo menos 1 o menos 4. El 1 de la cola (2 y 5)
// entra con la clave 0; los otros, en el segundo más el salto de cada repetición.
TEST_F(SlideBannerTest, KindsChooseTheQueueAndTheKey)
{
    Client::UI::QueueTheSlide(m_banner, 3, 4, "tres veces", 1, 2.0f, 0xff0000ffu);
    Client::UI::QueueTheSlide(m_banner, 1, 0, "de una", 2, 0.0f, 0xff00ff00u);
    Client::UI::QueueTheSlide(m_banner, 1, 0, "grueso", 3, 0.0f, 0xff00ff00u);

    ASSERT_EQ(m_banner.normal.entries.size(), 4U);
    auto entry = m_banner.normal.entries.begin();
    EXPECT_EQ(entry->first, 0U);
    EXPECT_EQ(entry->second.text, "de una");
    EXPECT_EQ(entry->second.kind, 1);
    ++entry;
    EXPECT_EQ(entry->first, 10U);
    EXPECT_EQ(entry->second.kind, 0);
    EXPECT_EQ(std::next(entry)->first, 14U);
    EXPECT_EQ(std::next(entry, 2)->first, 18U);
    ASSERT_EQ(m_banner.bold.entries.size(), 1U);
    EXPECT_EQ(m_banner.bold.entries.begin()->second.kind, -1);
}

// 0x46b72f..0x46b749: sin texto, con más de 30 repeticiones o con un tipo fuera de 0..5, nada.
TEST_F(SlideBannerTest, OutOfRangeRequestsQueueNothing)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "", 1, 0.0f, 0);
    Client::UI::QueueTheSlide(m_banner, 31, 0, "texto", 1, 0.0f, 0);
    Client::UI::QueueTheSlide(m_banner, 1, 0, "texto", 6, 0.0f, 0);
    Client::UI::QueueTheSlide(m_banner, 0, 0, "texto", 1, 0.0f, 0);

    EXPECT_TRUE(m_banner.normal.entries.empty());
    EXPECT_TRUE(m_banner.bold.entries.empty());
}

// 0x46add0 y 0x46a9c0: una cola sin nada agrega su primera entrada con un ancho de pantalla de espacios adelante, la
// velocidad por rx y el color; mientras pasa, no toma otra.
TEST_F(SlideBannerTest, AnIdleQueueTakesItsFirstEntry)
{
    Client::UI::QueueTheSlide(m_banner, 2, 0, "pasa", 1, 3.0f, 0xff112233u);

    ASSERT_TRUE(Step(100));

    EXPECT_EQ(m_banner.normal.text, std::string(SpacesAt640, ' ') + "pasa");
    EXPECT_EQ(m_banner.normal.color, 0xff112233u);
    EXPECT_EQ(m_banner.normal.maxSpeed, 3.0f);
    EXPECT_EQ(m_banner.normal.entries.size(), 1U);
    // 0x46a510: con texto y el pedazo en 0 no está libre.
    ASSERT_TRUE(Step(200));
    EXPECT_EQ(m_banner.normal.entries.size(), 1U);
}

// 0x46ada0: los segundos suben uno por paso con más de 1000 ms desde el último; la entrada espera su segundo.
TEST_F(SlideBannerTest, AnEntryWaitsForItsSecond)
{
    Client::UI::QueueTheSlide(m_banner, 2, 2, "espera", 1, 0.0f, 0);
    ASSERT_TRUE(Step(100));
    m_banner.normal.text.clear();

    ASSERT_TRUE(Step(1001));
    EXPECT_EQ(m_banner.normal.seconds, 11U);
    EXPECT_TRUE(m_banner.normal.text.empty());
    ASSERT_TRUE(Step(2002));
    EXPECT_EQ(m_banner.normal.seconds, 12U);
    EXPECT_FALSE(m_banner.normal.text.empty());
    // 0x46abc0: sin velocidad, 2,5.
    EXPECT_EQ(m_banner.normal.maxSpeed, 2.5f);
}

// 0x46ae07: una entrada de tipo −1 con más de 60 segundos sale sin agregarse.
TEST_F(SlideBannerTest, AnOldExpiringEntryIsDropped)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "vieja", 0, 4.0f, 0);
    m_banner.normal.seconds = 71;

    ASSERT_TRUE(Step(0));

    EXPECT_TRUE(m_banner.normal.entries.empty());
    EXPECT_TRUE(m_banner.normal.text.empty());
    EXPECT_EQ(m_banner.normal.maxSpeed, 4.0f);
}

// 0x46a9c0: lo que no entra en 0x400 bytes espera.
TEST_F(SlideBannerTest, ATextThatDoesNotFitWaits)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, std::string(1024 - SpacesAt640, 'x'), 2, 0.0f, 0);

    ASSERT_TRUE(Step(0));

    EXPECT_TRUE(m_banner.normal.text.empty());
    EXPECT_EQ(m_banner.normal.entries.size(), 1U);
}

// La gruesa pasa primero (0x46b7b0); sin medidor, la que tiene que agregar corta.
TEST_F(SlideBannerTest, WithoutAMeasureAddingIsCut)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "texto", 5, 0.0f, 0);

    EXPECT_FALSE(
        Client::UI::StepTheSlides(m_banner, SlideStepContext{.measure = nullptr, .dcFont = m_dcFont}).HasValue());
}

// 0x46b680: el primer tramo con el nivel menor o igual que su tope; un rand() módulo la cuenta.
TEST_F(SlideBannerTest, PickingDrawsOneRandomFromTheLevelGroup)
{
    Client::Data::Text::SlideTable table;
    table.periodSeconds = 60;
    table.speed = 2.5f;
    table.groups[0] = {.maxLevel = 15, .count = 2, .texts = {"bajo uno", "bajo dos"}};
    table.groups[1] = {.maxLevel = 30, .count = 0, .texts = {}};
    table.groups[2] = {.maxLevel = 50, .count = 3, .texts = {"alto"}};
    Client::UI::LoadTheSlides(m_banner, table);
    CrtRandom random(7);
    CrtRandom expected(7);

    const std::optional<std::string> low = Client::UI::PickASlide(m_banner, 15, random);
    const int first = expected.Next();
    ASSERT_TRUE(low.has_value());
    EXPECT_EQ(*low, first % 2 == 0 ? "bajo uno" : "bajo dos");
    // Cuenta 0: sin rand().
    EXPECT_FALSE(Client::UI::PickASlide(m_banner, 16, random).has_value());
    EXPECT_EQ(random.State(), expected.State());
    // Más que todos los topes: sin rand().
    EXPECT_FALSE(Client::UI::PickASlide(m_banner, 51, random).has_value());
    EXPECT_EQ(random.State(), expected.State());
    // Un lugar que la lista no tiene: el rand() sale igual.
    const std::optional<std::string> high = Client::UI::PickASlide(m_banner, 40, random);
    const int second = expected.Next();
    EXPECT_EQ(high.has_value(), second % 3 == 0);
    EXPECT_EQ(random.State(), expected.State());
}

// 0x46a540: la opacidad sube de a 30; las franjas (0x621aa0) y el texto con el modo 6 en x + corrimiento, y después
// corre (0x46a910). El alto de "Z" sube la y una vez.
TEST_F(SlideBannerTest, DrawingFadesInAndScrolls)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "hola", 2, 2.0f, 0xff112233u);
    ASSERT_TRUE(Step(0));

    ASSERT_TRUE(Draw());

    EXPECT_EQ(m_banner.normal.alpha, 30);
    EXPECT_EQ(m_banner.normal.lineHeight, RecordingTextDrawer::Height);
    EXPECT_EQ(m_banner.normal.y, 0x1ad - RecordingTextDrawer::Height);
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_TRUE(m_text.texts[0].clippedAtLeft);
    EXPECT_EQ(m_text.texts[0].x, 0);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Normal);
    // (max(30 − 25, 0) + 50) · 0xff / 255 = 55.
    EXPECT_EQ(m_colors.text, 0x37112233u);
    EXPECT_EQ(m_colors.background, 0U);
    EXPECT_EQ(m_banner.normal.velocity, 1.0f);
    EXPECT_EQ(m_banner.normal.scroll, -1.0f);
    EXPECT_EQ(m_banner.drawn, 1);
    // El pedazo de adelante: cuatro espacios.
    EXPECT_EQ(m_banner.normal.chunkBytes, 4U);
    EXPECT_EQ(m_banner.normal.chunkWidth, 4 * RecordingTextDrawer::LetterWidth);

    ASSERT_TRUE(Draw());
    EXPECT_EQ(m_banner.normal.alpha, 60);
    EXPECT_EQ(m_text.texts[1].x, -1);
    EXPECT_EQ(m_banner.normal.scroll, -3.0f);
}

// 0x46a850: con el corrimiento pasado del pedazo, lo saca y suma su ancho.
TEST_F(SlideBannerTest, ThePassedChunkIsDropped)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "hola", 2, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));
    ASSERT_TRUE(Draw());
    m_banner.normal.scroll = -25.0f;

    ASSERT_TRUE(Draw());

    EXPECT_EQ(m_banner.normal.text.size(), SpacesAt640 + 4 - 4);
    EXPECT_EQ(m_text.texts.back().x, -1);
}

// 0x46a910: con el mouse sobre la franja frena de a 1.
TEST_F(SlideBannerTest, TheMouseOverTheBandSlowsIt)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "hola", 2, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));
    ASSERT_TRUE(Draw());
    ASSERT_TRUE(Draw());
    EXPECT_EQ(m_banner.normal.velocity, 2.0f);
    m_mouseX = 10;
    m_mouseY = m_banner.normal.y;

    ASSERT_TRUE(Draw());

    EXPECT_EQ(m_banner.normal.push, -1.0f);
    EXPECT_EQ(m_banner.normal.velocity, 1.0f);
}

// 0x46b090: mientras pasa la gruesa, la normal se apaga dibujando; la gruesa espera a que la normal esté en 0.
TEST_F(SlideBannerTest, TheBoldQueueWaitsForTheNormalToFade)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "normal", 2, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));
    ASSERT_TRUE(Draw());
    ASSERT_TRUE(Draw());
    ASSERT_EQ(m_banner.normal.alpha, 60);
    Client::UI::QueueTheSlide(m_banner, 1, 0, "grueso", 5, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));
    ASSERT_FALSE(m_banner.bold.text.empty());

    ASSERT_TRUE(Draw());
    EXPECT_EQ(m_banner.normal.alpha, 30);
    EXPECT_EQ(m_banner.bold.alpha, 0);
    EXPECT_EQ(m_text.texts.back().style.font, FontKind::Normal);
    ASSERT_TRUE(Draw());
    EXPECT_EQ(m_banner.normal.alpha, 0);
    EXPECT_EQ(m_banner.bold.alpha, 30);
    EXPECT_EQ(m_text.texts.back().style.font, FontKind::Bold);
}

// 0x46a540: con 0x6c7a8c apagado no dibuja ni toca nada.
TEST_F(SlideBannerTest, SlidesOffDrawNothing)
{
    Client::UI::QueueTheSlide(m_banner, 1, 0, "hola", 2, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));

    ASSERT_TRUE(Draw(false));

    EXPECT_EQ(m_banner.normal.alpha, 0);
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_TRUE(m_sprites.Commands().empty());
}

// Q-C518: forzada a apagarse con el texto terminado, el corrimiento negativo y el pedazo viejo, el original escribe
// fuera del buffer (strncpy con un largo negativo): corta.
TEST_F(SlideBannerTest, DroppingMoreThanIsLeftIsCut)
{
    SlideQueue& normal = m_banner.normal;
    normal.alpha = 60;
    normal.chunkBytes = 3;
    normal.chunkWidth = 0;
    normal.scroll = -1.0f;
    Client::UI::QueueTheSlide(m_banner, 1, 0, "grueso", 5, 2.0f, 0xffffffffu);
    ASSERT_TRUE(Step(0));

    EXPECT_FALSE(Draw());
}
} // namespace
