#include "Chat/ChatLogWindow.h"

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Chat/ChatLog.h"
#include "Audio/SoundId.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The chat log's step (CL8 milestone 3; 06 § El chat): 0x45aa80 and 0x479960 with 0x45d760, 0x45d990, 0x45dec0 and
// 0x47a230's bar. The texts are invented.
namespace
{
using Client::Chat::ChatEntry;
using Client::Chat::ChatLogFrame;
using Client::Chat::ChatLogGlobals;
using Client::Chat::ChatLogId;
using Client::Chat::ChatLogState;
using Client::Chat::ChatLogStepResult;
using Client::Chat::ChatLogStepView;
using Client::Chat::ChatModes;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class ChatLogWindowTest : public ::testing::Test
{
protected:
    ChatLogState m_log;
    ChatLogFrame m_frame;
    ChatModes m_modes;
    bool m_leftClick = false;
    bool m_rightDown = false;
    std::int32_t m_wheel = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_hovered = 0;
    std::int32_t m_focused = 0;
    std::uint8_t m_mouseOnUi = 0;
    LetterMeasure m_letters;

    void Fill(int count, std::int32_t type = 0)
    {
        for (int i = 0; i < count; ++i)
            m_log.entries.push_back(ChatEntry{.name = "Ana", .text = "hola", .type = type});
    }

    ChatLogStepResult Step(std::int32_t x, std::int32_t y, ChatLogStepView view = {})
    {
        view.mouseX = x;
        view.mouseY = y;
        if (view.measure == nullptr)
            view.measure = &m_letters;
        const ChatLogGlobals globals{.leftClick = m_leftClick,
                                     .rightDown = m_rightDown,
                                     .wheel = m_wheel,
                                     .capture = m_capture,
                                     .hovered = m_hovered,
                                     .focusedList = m_focused,
                                     .mouseOnUi = m_mouseOnUi,
                                     .modes = m_modes};
        return Client::Chat::StepTheChatLog(m_log, m_frame, globals, view);
    }
};

// The list takes (x, y − h, w, h) from its bottom edge: with the mouse over it, it is the one under the mouse.
TEST_F(ChatLogWindowTest, TheListBecomesTheHoveredWindowUnlessAnotherOneHasTheMouse)
{
    static_cast<void>(Step(200, 300));
    EXPECT_EQ(m_hovered, ChatLogId);

    m_hovered = 0;
    m_capture = 3;
    m_wheel = 1;
    Fill(20);
    static_cast<void>(Step(430, 300));
    EXPECT_EQ(m_hovered, 0);
    EXPECT_EQ(m_wheel, 1);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x479960: over the bar, the wheel scrolls 3 rows per notch and is turned off.
TEST_F(ChatLogWindowTest, TheWheelOverTheBarScrollsThreeRowsPerNotch)
{
    Fill(20);
    m_wheel = 1;

    const ChatLogStepResult result = Step(430, 300);

    EXPECT_TRUE(result.handled);
    EXPECT_EQ(m_log.scroll, 3);
    EXPECT_EQ(m_wheel, 0);
    EXPECT_EQ(m_mouseOnUi, 1);
}

// The up arrow (x + w − 0x15, y − h + 8, 13, 13): one row and the 0x19 when pressed; then it counts up to 16 steps and
// from there scrolls on each one.
TEST_F(ChatLogWindowTest, TheUpArrowScrollsOnceAndRepeatsAfter16Steps)
{
    Fill(40);
    m_leftClick = true;

    const ChatLogStepResult first = Step(435, 180);
    EXPECT_EQ(m_log.scroll, 1);
    EXPECT_EQ(first.sounds, std::vector<Client::Audio::SoundId>{Client::Chat::ChatButtonSound});
    EXPECT_EQ(m_focused, ChatLogId);

    for (int step = 0; step < 15; ++step)
        static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.scroll, 1);
    EXPECT_EQ(m_log.arrowRepeat, 16);

    static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.scroll, 2);

    // Without the click, the counts go back to 0.
    m_leftClick = false;
    static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.arrowRepeat, 0);
}

// 0x45d760: the right click on the selected one sends it to the whisper field; the selection goes back to 0 on every
// step.
TEST_F(ChatLogWindowTest, TheRightClickOnTheSelectedRowWhispersToItsName)
{
    m_log.entries.push_back(ChatEntry{.name = "Beto", .text = "hola", .type = 0});
    m_log.entries.push_back(ChatEntry{.name = "Ana", .text = "chau", .type = 0});
    m_log.selection = 2;
    m_rightDown = true;

    const ChatLogStepResult result = Step(10, 10);

    EXPECT_EQ(result.whisperTo, std::optional<std::string>("Ana"));
    EXPECT_FALSE(m_rightDown);
    EXPECT_EQ(m_log.selection, 0);
}

// 0x45dec0: with the safe zone and the focus on the chat, the mouse over another's name selects it (scroll + 1 + row).
TEST_F(ChatLogWindowTest, TheMouseOverAnotherNameSelectsItsRow)
{
    m_frame.x = -10;
    m_frame.y = 0x65;
    m_log.entries.push_back(ChatEntry{.name = "Ana", .text = "hola", .type = 0});
    m_log.entries.push_back(ChatEntry{.name = "Beto", .text = "chau", .type = 0});
    ChatLogStepView view;
    view.heroSafeZone = true;
    view.chatEditFocused = true;
    view.heroName = "Beto";

    // Row 1 (y = 0x65 − 13 − 16): the hero's name is not selected.
    static_cast<void>(Step(5, 75, view));
    EXPECT_EQ(m_log.selection, 0);

    // Row 0, "Ana", 18 pixels wide from x = 0.
    static_cast<void>(Step(5, 88, view));
    EXPECT_EQ(m_log.selection, 1);
    EXPECT_EQ(m_log.entries[0].blink, 1);

    view.chatEditFocused = false;
    static_cast<void>(Step(5, 88, view));
    EXPECT_EQ(m_log.selection, 0);
}

// The top edge (x, y − h, w, 8) is grabbed with the click; the height follows the mouse in steps of 40 (3 rows) and
// releasing the click leaves it.
TEST_F(ChatLogWindowTest, TheTopEdgeResizesTheListInStepsOfThreeRows)
{
    Fill(20);
    m_leftClick = true;
    // Over the bar (so that 0x45d760 returns true) and over the edge.
    static_cast<void>(Step(430, 171));
    EXPECT_EQ(m_log.state, 1);
    EXPECT_EQ(m_capture, ChatLogId);

    static_cast<void>(Step(300, 300));
    EXPECT_EQ(m_log.rows, 9);

    m_leftClick = false;
    static_cast<void>(Step(300, 300));
    EXPECT_EQ(m_log.state, 0);
    EXPECT_EQ(m_capture, 0);
}

// 0x47a230: the track from y − h + 21 to y − 17, the thumb sized by rows / visible and placed by the scroll.
TEST_F(ChatLogWindowTest, TheBarFollowsTheRowsAndTheScroll)
{
    Fill(12);
    m_log.scroll = 3;

    Client::Chat::PlaceTheLogBar(m_log, m_modes, m_frame);

    EXPECT_FLOAT_EQ(m_frame.barTop, 191.0f);
    EXPECT_FLOAT_EQ(m_frame.barBottom, 403.0f);
    EXPECT_FLOAT_EQ(m_frame.thumbHeight, 106.0f);
    EXPECT_FLOAT_EQ(m_frame.thumbY, 244.0f);
}

// Dragging the thumb: the click grabs it and the scroll follows its position; released, the list stays still.
TEST_F(ChatLogWindowTest, DraggingTheThumbScrollsAndTheReleasedClickStopsIt)
{
    Fill(12);
    Client::Chat::PlaceTheLogBar(m_log, m_modes, m_frame);
    m_leftClick = true;
    // Not scrolled, the thumb is at the bottom: (x + w − 0x13, 297, 13, 106).
    static_cast<void>(Step(440, 360));
    EXPECT_EQ(m_log.state, 2);
    EXPECT_EQ(m_capture, ChatLogId);
    EXPECT_FLOAT_EQ(m_log.dragOffset, 63.0f);

    // At the very top: the oldest.
    static_cast<void>(Step(440, 100));
    EXPECT_EQ(m_log.scroll, 6);
    // At the bottom: the newest.
    static_cast<void>(Step(440, 400));
    EXPECT_EQ(m_log.scroll, 0);

    m_leftClick = false;
    static_cast<void>(Step(440, 100));
    EXPECT_EQ(m_log.state, 0);
    EXPECT_EQ(m_capture, 0);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x45d990: the buttons, with the mouse outside every window. The third raises the transparency by 0.2 and past 0.9
// goes back to 0.2; the first is F2.
TEST_F(ChatLogWindowTest, TheButtonsToggleF2AndCycleTheTransparency)
{
    m_log.buttonsShown = 1;
    m_leftClick = true;

    ChatLogStepResult result = Step(335, 425);
    EXPECT_FLOAT_EQ(m_log.alpha, 0.6f);
    EXPECT_FALSE(m_leftClick);
    EXPECT_EQ(result.sounds, std::vector<Client::Audio::SoundId>{Client::Chat::ChatButtonSound});

    m_leftClick = true;
    static_cast<void>(Step(335, 425));
    m_leftClick = true;
    static_cast<void>(Step(335, 425));
    EXPECT_FLOAT_EQ(m_log.alpha, 0.2f);

    m_leftClick = true;
    static_cast<void>(Step(300, 425));
    EXPECT_FALSE(m_modes.showChat);
}

// 0x45d760: the buttons show up with the mouse in (0xa0, 0x1b4, 0x140, 0x32) and go away with the mouse above 0x1a0.
TEST_F(ChatLogWindowTest, TheButtonsShowUpWithTheMouseBelow)
{
    static_cast<void>(Step(200, 440));
    EXPECT_EQ(m_log.buttonsShown, 1);
    static_cast<void>(Step(200, 418));
    EXPECT_EQ(m_log.buttonsShown, 1);
    static_cast<void>(Step(200, 300));
    EXPECT_EQ(m_log.buttonsShown, 0);
}
} // namespace
