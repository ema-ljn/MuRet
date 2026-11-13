#include "Scenes/WorldTexts.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GameProjection.h"
#include "Scenes/WorldRuntime.h"
#include "support/RecordingTextDrawer.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

// Los textos 3D del juego (CL8 hito 6; 06 § HUD y ventanas, "La pasada 2D del juego"): 0x4383f0. La proyección de los
// tests deja la x de la pantalla en el centro menos la x y la y en el centro más la z menos 300, con el ojo siempre
// a 1: con los 370 de arriba, en 240 + z + 70. El texto mide como el rasterizador falso: 6 columnas por letra y 13
// filas. Los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Scenes::GameProjection;
using Client::Scenes::HudState;
using Client::Scenes::WorldText;
using Client::Scenes::WorldTextDrawContext;
using Client::Scenes::WorldTextQueue;
using Client::Tests::RecordingTextDrawer;

constexpr PixelSize Screen640{640, 480};

GameProjection FlatProjection(PixelSize screen)
{
    GameProjection projection;
    projection.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, -300.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    projection.horizontalScale = 1.0f;
    projection.verticalScale = 1.0f;
    projection.centerX = screen.width / 2;
    projection.centerY = screen.height / 2;
    projection.screenWidth = screen.width;
    projection.screenHeight = screen.height;
    return projection;
}

WorldText Entry(std::string_view text, glm::vec3 position)
{
    WorldText entry;
    std::copy_n(text.begin(), std::min(text.size(), entry.text.size()), entry.text.begin());
    entry.position = position;
    return entry;
}

class WorldTextsTest : public ::testing::Test
{
protected:
    WorldTextsTest() : m_text(m_sprites), m_projection(FlatProjection(Screen640))
    {
        m_sprites.Begin(Screen640);
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, bool withProjection = true)
    {
        WorldTextDrawContext context{.queue = m_queue,
                                     .projection = withProjection ? &m_projection : nullptr,
                                     .sprites = m_sprites,
                                     .text = m_text,
                                     .hud = m_hud,
                                     .projecting = projecting};
        return Client::Scenes::DrawTheWorldTexts(context);
    }

    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    GameProjection m_projection;
    WorldTextQueue m_queue;
    HudState m_hud{};
};

// 0x43840b..0x438584: cada entrada, 370 más arriba, centrada en la x con la negrita, el fondo 0xff000064 y el texto
// 0xffc8e6ff; la cola queda vacía.
TEST_F(WorldTextsTest, EachEntryIsDrawnAboveItsPositionAndTheQueueEmpties)
{
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    m_queue.push_back(Entry("Paso", glm::vec3(-20.0f, 0.0f, -200.0f)));
    ASSERT_TRUE(Draw().HasValue());

    EXPECT_TRUE(m_queue.empty());
    ASSERT_EQ(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[0].text, "Puerta");
    EXPECT_EQ(m_text.texts[0].x, 220 - 18);
    EXPECT_EQ(m_text.texts[0].y, 210);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffc8e6ffu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0xff000064u);
    EXPECT_EQ(m_text.texts[1].text, "Paso");
    EXPECT_EQ(m_text.texts[1].x, 340 - 12);
    EXPECT_EQ(m_text.texts[1].y, 110);
    // 0x554180 guarda el tamaño del último; la fuente y los colores quedan.
    EXPECT_EQ(m_hud.arrowsTextWidth, 24);
    EXPECT_EQ(m_hud.arrowsTextHeight, 13);
    EXPECT_EQ(m_hud.dcFont, FontKind::Bold);
    EXPECT_EQ(m_hud.textColors.text, 0xffc8e6ffu);
}

// 0x438460..0x438469: con la x o la y de la pantalla negativas no se dibuja, pero sale de la cola.
TEST_F(WorldTextsTest, AnEntryOffTheScreenIsDroppedWithoutDrawing)
{
    m_queue.push_back(Entry("Lejos", glm::vec3(400.0f, 0.0f, 0.0f)));
    m_queue.push_back(Entry("Abajo", glm::vec3(0.0f, 0.0f, -400.0f)));
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(m_queue.empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_hud.dcFont, FontKind::Normal);
}

// strlen sobre los 64 bytes: sin 0, los 64.
TEST_F(WorldTextsTest, AFullEntryReadsItsSixtyFourBytes)
{
    const std::string full(Client::Scenes::WorldTextSize, 'a');
    m_queue.push_back(Entry(full, glm::vec3(0.0f, 0.0f, 0.0f)));
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, full);
}

// Un dibujo intermedio de MuRet lee la cola sin vaciarla.
TEST_F(WorldTextsTest, AProjectedDrawKeepsTheQueue)
{
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(m_queue.size(), 1U);
    EXPECT_EQ(m_text.texts.size(), 1U);
}

// Sin la proyección del pase del mundo, la cola vacía no corta y una entrada sí (0x438455).
TEST_F(WorldTextsTest, AnEntryWithoutTheProjectionCuts)
{
    ASSERT_TRUE(Draw(false, false).HasValue());
    m_queue.push_back(Entry("Puerta", glm::vec3(100.0f, 0.0f, -100.0f)));
    const auto drawn = Draw(false, false);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x438455"), std::string::npos);
}
} // namespace
