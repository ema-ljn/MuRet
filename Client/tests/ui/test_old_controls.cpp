#include "Ui/OldControls.h"

#include "Ui/OldWindowBase.h"
#include "Audio/SoundId.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// The old windows' controls (CL9 milestone 9): the list template of 0x47b0f0, 0x47a9d0, 0x47a910, 0x461830,
// 0x47aef0..0x47afd0, 0x47b9c0 and 0x461c70. The lists have no elements of their own: the test says how many there
// are.
namespace
{
using Client::Ui::ArrowDownMessage;
using Client::Ui::ArrowUpMessage;
using Client::Ui::OldList;
using Client::Ui::OldListGlobals;
using Client::Ui::OldListView;
using Client::Ui::OldMessage;
using Client::Ui::SizeMessage;
using Client::Ui::ToTheEndMessage;

class OldListTest : public ::testing::Test
{
protected:
    OldList m_list;
    bool m_leftClick = false;
    std::int32_t m_wheel = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_focused = 0;
    std::uint8_t m_mouseOnUi = 0;
    std::optional<Client::Audio::SoundId> m_sound;
    bool m_drawn = true;
    std::int32_t m_modal = 0;
    std::set<std::uint8_t> m_down;

    void SetUp() override
    {
        // Like the friend list in its tab: the rectangle hangs from y = 300, 0xe6 × 0x5b.
        m_list.base.id = 40;
        m_list.base.parent = 39;
        m_list.base.x = 100;
        m_list.base.y = 300;
        m_list.base.width = 0xe6;
        m_list.base.height = 0x5b;
        m_list.base.rectMode = 2;
    }

    [[nodiscard]] OldListGlobals Globals()
    {
        return OldListGlobals{.leftClick = m_leftClick,
                              .wheel = m_wheel,
                              .capture = m_capture,
                              .focusedList = m_focused,
                              .mouseOnUi = m_mouseOnUi,
                              .sound = m_sound,
                              .drawn = m_drawn,
                              .modal = m_modal};
    }

    [[nodiscard]] OldListView View(std::int32_t x, std::int32_t y) const
    {
        return OldListView{
            .mouseX = x, .mouseY = y, .keyDown = [this](std::uint8_t key) { return m_down.contains(key); }};
    }

    bool Step(std::size_t size, std::int32_t x, std::int32_t y,
              std::vector<std::pair<std::int32_t, std::size_t>>* rows = nullptr)
    {
        return Client::Ui::StepTheListMouse(m_list, size, Globals(), View(x, y),
                                            [rows](std::int32_t row, std::size_t at)
                                            {
                                                if (rows != nullptr)
                                                    rows->emplace_back(row, at);
                                                return 1;
                                            });
    }

    void Handle(std::int32_t type, std::size_t size)
    {
        m_list.base.current = OldMessage{.type = type, .a = 0, .b = 0, .text = {}};
        Client::Ui::HandleTheListMessage(m_list, size);
    }
};

TEST_F(OldListTest, ScrollingNeedsAsManyAsTheRowsAndStaysInRange)
{
    Client::Ui::ScrollTheList(m_list, 5, -3);
    EXPECT_EQ(m_list.scroll, 0);
    Client::Ui::ScrollTheList(m_list, 10, -3);
    EXPECT_EQ(m_list.scroll, 3);
    Client::Ui::ScrollTheList(m_list, 10, -100);
    EXPECT_EQ(m_list.scroll, 4);
    Client::Ui::ScrollTheList(m_list, 10, 100);
    EXPECT_EQ(m_list.scroll, 0);
}

// 0x461830: fewer rows than before and than the count scroll one; more than the count go back to 0.
TEST_F(OldListTest, FewerRowsScrollOneMore)
{
    m_list.scroll = 2;
    Client::Ui::SetTheListRows(m_list, 10, 4);
    EXPECT_EQ(m_list.rows, 4);
    EXPECT_EQ(m_list.scroll, 3);
    Client::Ui::SetTheListRows(m_list, 10, 12);
    EXPECT_EQ(m_list.rows, 12);
    EXPECT_EQ(m_list.scroll, 0);
}

TEST_F(OldListTest, TheSelectionMovesInsideTheCount)
{
    Client::Ui::SelectTheNext(m_list, 3, 1);
    EXPECT_EQ(m_list.selection, 0);
    Client::Ui::SelectInTheList(m_list, 3, 9);
    EXPECT_EQ(m_list.selection, 3);
    Client::Ui::SelectTheNext(m_list, 3, 1);
    EXPECT_EQ(m_list.selection, 3);
    Client::Ui::SelectThePrevious(m_list, 3, 5);
    EXPECT_EQ(m_list.selection, 1);
    EXPECT_EQ(Client::Ui::SelectedInTheList(m_list, 3), std::optional<std::size_t>(0));
    EXPECT_EQ(Client::Ui::SelectedInTheList(m_list, 0), std::nullopt);
    EXPECT_EQ(m_list.selection, 0);
}

// 0x47a9d0: message 9 counts with the previous height (0x5b · 1/13 − 0.5 = 6.5 → 6); 0xe goes to the end and chooses
// the last one.
TEST_F(OldListTest, TheMessagesCountRowsAndGoToTheEnd)
{
    m_list.rows = 3;
    Handle(SizeMessage, 20);
    EXPECT_EQ(m_list.rows, 6);
    Handle(ToTheEndMessage, 20);
    EXPECT_EQ(m_list.scroll, 14);
    EXPECT_EQ(m_list.selection, 20);
}

// 0xf moves the selection up and 0x10 down, and the scroll follows it.
TEST_F(OldListTest, TheKeyboardArrowsKeepTheSelectionInSight)
{
    Client::Ui::SelectInTheList(m_list, 20, 6);
    Handle(ArrowUpMessage, 20);
    EXPECT_EQ(m_list.selection, 7);
    EXPECT_EQ(m_list.scroll, 1);
    Client::Ui::SelectInTheList(m_list, 20, 2);
    Handle(ArrowDownMessage, 20);
    EXPECT_EQ(m_list.selection, 1);
    EXPECT_EQ(m_list.scroll, 0);
}

// 0x47b11f: with the list focused, the arrow sends its message on the press and on every step from the sixth.
TEST_F(OldListTest, AHeldArrowRepeatsFromTheSixthStep)
{
    m_focused = 40;
    m_down.insert(0x26);
    for (int step = 0; step < 8; ++step)
        static_cast<void>(Step(20, 0, 0));
    ASSERT_EQ(m_list.base.queue.size(), 3u);
    EXPECT_EQ(m_list.base.queue.front().type, ArrowUpMessage);
    m_down.clear();
    static_cast<void>(Step(20, 0, 0));
    EXPECT_EQ(m_list.keyRepeat, 0);
    m_focused = 0;
    m_down.insert(0x28);
    static_cast<void>(Step(20, 0, 0));
    EXPECT_EQ(m_list.base.queue.size(), 3u);
}

TEST_F(OldListTest, TheWheelScrollsThreeRowsPerStep)
{
    m_wheel = 1;
    EXPECT_TRUE(Step(20, 150, 250));
    EXPECT_EQ(m_list.scroll, 3);
    EXPECT_EQ(m_wheel, 0);
    EXPECT_EQ(m_mouseOnUi, 1);
}

// The bar's up arrow (x + 8 − 0x15 + w, y − h) scrolls one with the sound; with a parent, the count only rises with
// the manager drawn.
TEST_F(OldListTest, TheBarArrowCountsOnlyAfterADraw)
{
    m_leftClick = true;
    const std::int32_t arrowX = 100 + 8 - 0x15 + 0xe6;
    EXPECT_TRUE(Step(20, arrowX + 1, 300 - 0x5b + 1));
    EXPECT_EQ(m_list.scroll, 1);
    EXPECT_EQ(m_sound, Client::Audio::SoundId{0x19});
    EXPECT_EQ(m_focused, 40);
    m_drawn = false;
    static_cast<void>(Step(20, arrowX + 1, 300 - 0x5b + 1));
    EXPECT_EQ(m_list.arrowRepeat, 1);
    m_drawn = true;
    static_cast<void>(Step(20, arrowX + 1, 300 - 0x5b + 1));
    EXPECT_EQ(m_list.arrowRepeat, 2);
    m_leftClick = false;
    static_cast<void>(Step(20, arrowX + 1, 300 - 0x5b + 1));
    EXPECT_EQ(m_list.arrowRepeat, 0);
}

TEST_F(OldListTest, DraggingTheThumbToTheTopScrollsToTheEnd)
{
    Client::Ui::PlaceTheListBar(m_list, 20);
    EXPECT_FLOAT_EQ(m_list.barTop, static_cast<float>(300 - 0x5b + 0xc));
    EXPECT_FLOAT_EQ(m_list.barBottom, 288.0f);
    m_leftClick = true;
    const std::int32_t barX = 100 + 8 - 0x13 + 0xe6;
    const auto thumbY = static_cast<std::int32_t>(m_list.thumbY);
    EXPECT_TRUE(Step(20, barX + 1, thumbY + 1));
    EXPECT_EQ(m_list.base.state, 2);
    EXPECT_EQ(m_capture, 40);
    static_cast<void>(Step(20, barX + 1, 0));
    EXPECT_EQ(m_list.scroll, 14);
    m_leftClick = false;
    static_cast<void>(Step(20, barX + 1, 0));
    EXPECT_EQ(m_list.base.state, 0);
    EXPECT_EQ(m_capture, 0);
}

// 0x47b714: the rows from the scroll, up to the rows or the end; with a modal box, none.
TEST_F(OldListTest, TheRowsAreWalkedFromTheScroll)
{
    m_list.scroll = 2;
    std::vector<std::pair<std::int32_t, std::size_t>> rows;
    static_cast<void>(Step(5, 0, 0, &rows));
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows.front(), (std::pair<std::int32_t, std::size_t>{0, 2}));
    EXPECT_EQ(rows.back(), (std::pair<std::int32_t, std::size_t>{2, 4}));
    rows.clear();
    m_modal = 7;
    static_cast<void>(Step(5, 0, 0, &rows));
    EXPECT_TRUE(rows.empty());
}

// 0x461c70: the others go up by 13.
TEST_F(OldListTest, TheFirstShownRowIsTheLowest)
{
    EXPECT_EQ(Client::Ui::ListRowY(m_list, 20, 0), 6 * 13 - 0x5b - 10 + 300);
    EXPECT_EQ(Client::Ui::ListRowY(m_list, 20, 5), 13 - 0x5b - 10 + 300);
    EXPECT_EQ(Client::Ui::ListRowY(m_list, 2, 0), 2 * 13 - 0x5b - 10 + 300);
}
} // namespace
