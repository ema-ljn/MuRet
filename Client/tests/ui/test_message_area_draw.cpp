#include "UI/MessageArea.h"

#include "App/WindowState.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

// Client::UI::DrawTheMessageArea (0x5554e0). The text is invented.
namespace
{
using Client::App::PixelSize;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Tests::RecordingTextDrawer;
using Client::UI::MessageAreaState;
using Client::UI::MessageAreaView;
using Client::UI::MessageDrawContext;
using Client::UI::MessageFrameState;

constexpr PixelSize Screen640{640, 480};
constexpr int RowHeight = 13;
constexpr int CrywolfOffset = 0x89;

class MessageAreaDrawTest : public ::testing::Test
{
protected:
    MessageAreaDrawTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

    void PutRow(std::size_t shown)
    {
        m_area.rows[static_cast<std::size_t>(Client::UI::FirstShownRow) + shown].text = "invented";
    }

    void DrawAt(std::uint8_t crywolfStage)
    {
        m_text.texts.clear();
        m_view.crywolfStage = crywolfStage;
        const MessageDrawContext context{.sprites = m_sprites,
                                         .text = m_text,
                                         .dcFont = m_dcFont,
                                         .textColors = m_colors,
                                         .textWidth = m_textWidth,
                                         .textHeight = m_textHeight};
        Client::UI::DrawTheMessageArea(m_area, m_frame, context, m_view);
    }

    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
    std::int32_t m_textWidth = 0;
    std::int32_t m_textHeight = 0;
    MessageAreaState m_area;
    MessageFrameState m_frame;
    MessageAreaView m_view;
};

// 0x5554e0 reads 0x723c05: only with 3 or 4 the rows go 0x89 lower.
TEST_F(MessageAreaDrawTest, OnlyTheCrywolfStagesThreeAndFourLowerTheRows)
{
    PutRow(0);
    PutRow(2);

    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{3, 4})
    {
        DrawAt(stage);

        ASSERT_EQ(m_text.texts.size(), 2u) << "stage " << int{stage};
        EXPECT_EQ(m_text.texts[0].y, CrywolfOffset) << "stage " << int{stage};
        EXPECT_EQ(m_text.texts[1].y, 2 * RowHeight + CrywolfOffset) << "stage " << int{stage};
    }
    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{0, 1, 2, 5, 6, 0xff})
    {
        DrawAt(stage);

        ASSERT_EQ(m_text.texts.size(), 2u) << "stage " << int{stage};
        EXPECT_EQ(m_text.texts[0].y, 0) << "stage " << int{stage};
        EXPECT_EQ(m_text.texts[1].y, 2 * RowHeight) << "stage " << int{stage};
    }
}
} // namespace
