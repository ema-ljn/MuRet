#include "UI/ChatLogDraw.h"

#include "App/WindowState.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "UI/ChatLog.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// El dibujo del registro del chat (CL8 hito 3; 06 § El chat): 0x479080 con el marco (0x45c460), los renglones
// (0x45cbb0) y los botones (0x45d420). Los textos son inventados.
namespace
{
using Client::App::PixelSize;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;
using Client::UI::ChatEntry;
using Client::UI::ChatLogDrawContext;
using Client::UI::ChatLogDrawView;
using Client::UI::ChatLogFrame;
using Client::UI::ChatLogState;
using Client::UI::ChatModes;

constexpr PixelSize Screen640{640, 480};

class ChatLogDrawTest : public ::testing::Test
{
protected:
    ChatLogDrawTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
    ChatLogState m_log;
    ChatLogFrame m_frame;
    ChatModes m_modes;
    ChatLogDrawView m_view;

    void Draw()
    {
        const ChatLogDrawContext context{
            .sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
        Client::UI::DrawTheChatLog(m_log, m_frame, m_modes, context, m_view);
    }

    // El registro fuera de su ventana, donde lo pone el paso de la UI: (−10, 0x65).
    void AtTheTopLeft()
    {
        m_frame.x = -10;
        m_frame.y = 0x65;
    }

    void Push(std::string name, std::string text, std::int32_t type, std::uint32_t age = 0)
    {
        m_log.entries.push_back(ChatEntry{.name = std::move(name), .text = std::move(text), .type = type, .age = age});
    }
};

// 0x45cbb0: el nombre con ": " en x + 10 y el texto después, a ftol(cx / rx); de abajo hacia arriba desde y − 16.
TEST_F(ChatLogDrawTest, RowsGoUpFromTheBottomWithTheNameBeforeTheText)
{
    AtTheTopLeft();
    Push("Ana", "hola", 0);
    Push("", "suelto", 1);

    Draw();

    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[0].text, "Ana: ");
    EXPECT_EQ(m_text.texts[0].x, 0);
    EXPECT_EQ(m_text.texts[0].y, 85);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0xff000000u, .background = 0x9632c8ffu}));
    EXPECT_EQ(m_text.texts[1].text, "hola");
    EXPECT_EQ(m_text.texts[1].x, 30);
    EXPECT_EQ(m_text.texts[1].y, 85);
    EXPECT_EQ(m_text.texts[2].text, "suelto");
    EXPECT_EQ(m_text.texts[2].x, 0);
    EXPECT_EQ(m_text.texts[2].y, 72);
    EXPECT_EQ(m_text.texts[2].style.colors, (TextColors{.text = 0xffff9664u, .background = 0x96000000u}));
    EXPECT_EQ(m_dcFont, FontKind::Normal);
    EXPECT_EQ(m_frame.blink, 1);
}

// El chat normal tiene un nombre (0xffefdccd) más claro que su texto (0xffc8dce6).
TEST_F(ChatLogDrawTest, NormalChatHasALighterName)
{
    AtTheTopLeft();
    Push("Ana", "hola", 3);

    Draw();

    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffefdccdu);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xffc8dce6u);
}

// Fuera de su ventana y sin correr, la edad sube el renglón; con más edad que lugar, corta el dibujo.
TEST_F(ChatLogDrawTest, AgeLiftsTheRowAndTooMuchAgeEndsTheDrawing)
{
    AtTheTopLeft();
    Push("Ana", "uno", 0, 2);
    Push("Beto", "dos", 0);

    Draw();
    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].y, 59);
    EXPECT_EQ(m_text.texts[2].y, 72);

    m_text.texts.clear();
    m_log.entries[0].age = 6;
    Draw();
    EXPECT_TRUE(m_text.texts.empty());
}

// Sin F2, el chat normal no ocupa renglón.
TEST_F(ChatLogDrawTest, HiddenRowsTakeNoPlace)
{
    AtTheTopLeft();
    m_modes.showChat = false;
    Push("Ana", "a", 3);
    Push("Beto", "b", 0);

    Draw();

    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].text, "Beto: ");
    EXPECT_EQ(m_text.texts[0].y, 85);
}

// El elegido cambia el texto y el fondo de su nombre tres dibujos de cada seis.
TEST_F(ChatLogDrawTest, TheSelectedNameBlinks)
{
    AtTheTopLeft();
    Push("Ana", "hola", 0);
    m_log.selection = 1;

    Draw();

    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0x9632c8ffu, .background = 0xff000000u}));
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = 0xff000000u, .background = 0x9632c8ffu}));
}

// 0x45c460 en su ventana: el alto de 40 cada 3 renglones más 10 y un fondo negro con alfa 1 − 0,4, sin textura.
TEST_F(ChatLogDrawTest, InItsWindowTheFrameSizesToTheRowsWithATranslucentBackground)
{
    m_modes.placement = 1;

    Draw();

    EXPECT_EQ(m_frame.height, 90);
    EXPECT_FLOAT_EQ(m_frame.barTop, 351.0f);
    EXPECT_FLOAT_EQ(m_frame.barBottom, 403.0f);
    const std::span<const SpriteVertex> vertices = m_sprites.Vertices();
    ASSERT_GE(vertices.size(), 6u);
    EXPECT_NEAR(vertices[0].x, 186.0f, 1e-4f);
    EXPECT_NEAR(vertices[0].y, 480.0f - 330.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].x, 186.0f + 268.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].y, 480.0f - 432.0f, 1e-4f);
    EXPECT_NEAR(vertices[0].color.alpha, 0.6f, 1e-6f);
    EXPECT_FALSE(m_sprites.Commands()[0].texture.has_value());
}

// El título del filtro, "754: palabras", en negrita sobre la esquina, también fuera de su ventana.
TEST_F(ChatLogDrawTest, TheFilterTitleIsDrawnInBold)
{
    AtTheTopLeft();
    const auto texts = Client::Tests::MakeTextTable({{Client::UI::FilterTitleText, "Filtro"}});
    m_view.texts = &texts;
    m_log.filter[0] = "dragon";
    m_log.filter[1] = "alas";

    Draw();

    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_text.texts[0].text, "Filtro: dragon, alas");
    EXPECT_EQ(m_text.texts[0].x, 0);
    EXPECT_EQ(m_text.texts[0].y, 0x65);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0xffc8dce6u, .background = 0x96000000u}));
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// 0x45d420: los botones aparecen y se van de a 0,2 por dibujo, y con el campo del chat abierto no se ven.
TEST_F(ChatLogDrawTest, TheButtonsFadeInAndOut)
{
    AtTheTopLeft();
    m_log.buttonsShown = 1;
    for (int i = 0; i < 6; ++i)
        Draw();
    EXPECT_FLOAT_EQ(m_frame.buttonsAlpha, 1.0f);
    EXPECT_FALSE(m_sprites.Vertices().empty());

    m_log.buttonsShown = 0;
    Draw();
    EXPECT_FLOAT_EQ(m_frame.buttonsAlpha, 0.8f);

    m_sprites.Begin(Screen640);
    m_view.chatOpen = true;
    Draw();
    EXPECT_TRUE(m_sprites.Vertices().empty());
}

// En Crywolf (0x723c05 en 3 o 4), la lista de x = 0xba baja a y = 0x12f y los renglones van 0x65 por debajo.
TEST_F(ChatLogDrawTest, InCrywolfTheListMovesAndRowsHangBelow)
{
    m_view.crywolfStage = 3;
    Push("Ana", "hola", 0);

    Draw();

    EXPECT_EQ(m_frame.y, 0x12f);
    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].x, 0xba + 10);
    EXPECT_EQ(m_text.texts[0].y, 0x12f + 0x65);
}
} // namespace
