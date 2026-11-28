#include "Chat/ChatLogDraw.h"

#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Chat/ChatLog.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The chat log's draw (CL8 milestone 3; 06 § El chat): 0x479080 with the frame (0x45c460), the rows (0x45cbb0) and the
// buttons (0x45d420). The texts are invented.
namespace
{
using Client::Chat::ChatEntry;
using Client::Chat::ChatLogDrawContext;
using Client::Chat::ChatLogDrawView;
using Client::Chat::ChatLogFrame;
using Client::Chat::ChatLogState;
using Client::Chat::ChatModes;
using Client::Core::PixelSize;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

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
        Client::Chat::DrawTheChatLog(m_log, m_frame, m_modes, context, m_view);
    }

    // The log outside its window, where the UI step puts it: (−10, 0x65).
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

// 0x45cbb0: the name with ": " at x + 10 and the text after it, at ftol(cx / rx); bottom to top from y − 16.
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

// Normal chat has a name (0xffefdccd) lighter than its text (0xffc8dce6).
TEST_F(ChatLogDrawTest, NormalChatHasALighterName)
{
    AtTheTopLeft();
    Push("Ana", "hola", 3);

    Draw();

    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffefdccdu);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xffc8dce6u);
}

// Outside its window and not scrolled, the age lifts the row; with more age than room, it ends the draw.
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

// Without F2, normal chat takes no row.
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

// The selected one changes the text and its name's background three draws out of six.
TEST_F(ChatLogDrawTest, TheSelectedNameBlinks)
{
    AtTheTopLeft();
    Push("Ana", "hola", 0);
    m_log.selection = 1;

    Draw();

    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0x9632c8ffu, .background = 0xff000000u}));
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = 0xff000000u, .background = 0x9632c8ffu}));
}

// 0x45c460 in its window: a height of 40 every 3 rows plus 10 and a black background with alpha 1 − 0.4, without a
// texture.
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

// The filter title, "754: words", in bold over the corner, also outside its window.
TEST_F(ChatLogDrawTest, TheFilterTitleIsDrawnInBold)
{
    AtTheTopLeft();
    const auto texts = Client::Tests::MakeTextTable({{Client::Chat::FilterTitleText, "Filtro"}});
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

// 0x45d420: the buttons fade in and out by 0.2 per draw, and with the chat field open they are not shown.
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

// In Crywolf (0x723c05 at 3 or 4), the list at x = 0xba moves down to y = 0x12f and the rows go 0x65 below.
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

// 0x45cbb0 reads 0x723c05: only 3 and 4 move the list; with any other stage the row stays above the bottom edge.
TEST_F(ChatLogDrawTest, OnlyTheCrywolfStagesThreeAndFourMoveTheList)
{
    Push("Ana", "hola", 0);

    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{0, 1, 2, 5, 6, 0xff})
    {
        m_text.texts.clear();
        m_view.crywolfStage = stage;

        Draw();

        EXPECT_EQ(m_frame.y, 0x1a4) << "stage " << int{stage};
        ASSERT_EQ(m_text.texts.size(), 2u) << "stage " << int{stage};
        EXPECT_EQ(m_text.texts[0].y, 0x1a4 - 0x10) << "stage " << int{stage};
    }

    m_text.texts.clear();
    m_view.crywolfStage = 4;

    Draw();

    EXPECT_EQ(m_frame.y, 0x12f);
    ASSERT_EQ(m_text.texts.size(), 2u);
    EXPECT_EQ(m_text.texts[0].y, 0x12f + 0x65);
}
} // namespace
