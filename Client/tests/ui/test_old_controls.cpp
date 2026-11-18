#include "UI/OldControls.h"

#include "UI/OldWindowBase.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

// Los controles de las ventanas viejas (CL9 hito 9): la plantilla de lista de 0x47b0f0, 0x47a9d0, 0x47a910,
// 0x461830, 0x47aef0..0x47afd0, 0x47b9c0 y 0x461c70. Las listas no tienen elementos propios: el test dice cuántos hay.
namespace
{
using Client::UI::ArrowDownMessage;
using Client::UI::ArrowUpMessage;
using Client::UI::OldList;
using Client::UI::OldListGlobals;
using Client::UI::OldListView;
using Client::UI::OldMessage;
using Client::UI::SizeMessage;
using Client::UI::ToTheEndMessage;

class OldListTest : public ::testing::Test
{
protected:
    OldList m_list;
    bool m_leftClick = false;
    std::int32_t m_wheel = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_focused = 0;
    std::uint8_t m_mouseOnUi = 0;
    std::optional<std::int32_t> m_sound;
    bool m_drawn = true;
    std::int32_t m_modal = 0;
    std::set<std::uint8_t> m_down;

    void SetUp() override
    {
        // Como la de amigos en su pestaña: el rectángulo cuelga de y = 300, de 0xe6 × 0x5b.
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
        return Client::UI::StepTheListMouse(m_list, size, Globals(), View(x, y),
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
        Client::UI::HandleTheListMessage(m_list, size);
    }
};

TEST_F(OldListTest, ScrollingNeedsAsManyAsTheRowsAndStaysInRange)
{
    Client::UI::ScrollTheList(m_list, 5, -3);
    EXPECT_EQ(m_list.scroll, 0);
    Client::UI::ScrollTheList(m_list, 10, -3);
    EXPECT_EQ(m_list.scroll, 3);
    Client::UI::ScrollTheList(m_list, 10, -100);
    EXPECT_EQ(m_list.scroll, 4);
    Client::UI::ScrollTheList(m_list, 10, 100);
    EXPECT_EQ(m_list.scroll, 0);
}

// 0x461830: menos renglones que antes y que la cuenta corren uno; más que la cuenta vuelven a 0.
TEST_F(OldListTest, FewerRowsScrollOneMore)
{
    m_list.scroll = 2;
    Client::UI::SetTheListRows(m_list, 10, 4);
    EXPECT_EQ(m_list.rows, 4);
    EXPECT_EQ(m_list.scroll, 3);
    Client::UI::SetTheListRows(m_list, 10, 12);
    EXPECT_EQ(m_list.rows, 12);
    EXPECT_EQ(m_list.scroll, 0);
}

TEST_F(OldListTest, TheSelectionMovesInsideTheCount)
{
    Client::UI::SelectTheNext(m_list, 3, 1);
    EXPECT_EQ(m_list.selection, 0);
    Client::UI::SelectInTheList(m_list, 3, 9);
    EXPECT_EQ(m_list.selection, 3);
    Client::UI::SelectTheNext(m_list, 3, 1);
    EXPECT_EQ(m_list.selection, 3);
    Client::UI::SelectThePrevious(m_list, 3, 5);
    EXPECT_EQ(m_list.selection, 1);
    EXPECT_EQ(Client::UI::SelectedInTheList(m_list, 3), std::optional<std::size_t>(0));
    EXPECT_EQ(Client::UI::SelectedInTheList(m_list, 0), std::nullopt);
    EXPECT_EQ(m_list.selection, 0);
}

// 0x47a9d0: el 9 cuenta con el alto de antes (0x5b · 1/13 − 0,5 = 6,5 → 6); el 0xe va al final y elige el último.
TEST_F(OldListTest, TheMessagesCountRowsAndGoToTheEnd)
{
    m_list.rows = 3;
    Handle(SizeMessage, 20);
    EXPECT_EQ(m_list.rows, 6);
    Handle(ToTheEndMessage, 20);
    EXPECT_EQ(m_list.scroll, 14);
    EXPECT_EQ(m_list.selection, 20);
}

// 0xf sube la elección y 0x10 la baja, y el corrimiento la sigue.
TEST_F(OldListTest, TheKeyboardArrowsKeepTheSelectionInSight)
{
    Client::UI::SelectInTheList(m_list, 20, 6);
    Handle(ArrowUpMessage, 20);
    EXPECT_EQ(m_list.selection, 7);
    EXPECT_EQ(m_list.scroll, 1);
    Client::UI::SelectInTheList(m_list, 20, 2);
    Handle(ArrowDownMessage, 20);
    EXPECT_EQ(m_list.selection, 1);
    EXPECT_EQ(m_list.scroll, 0);
}

// 0x47b11f: con la lista con el foco, la flecha se manda su mensaje al apretarla y desde el sexto paso en cada paso.
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

// La flecha de arriba de la barra (x + 8 − 0x15 + w, y − h) corre uno con el sonido; con padre, la cuenta solo sube con
// el administrador dibujado.
TEST_F(OldListTest, TheBarArrowCountsOnlyAfterADraw)
{
    m_leftClick = true;
    const std::int32_t arrowX = 100 + 8 - 0x15 + 0xe6;
    EXPECT_TRUE(Step(20, arrowX + 1, 300 - 0x5b + 1));
    EXPECT_EQ(m_list.scroll, 1);
    EXPECT_EQ(m_sound, 0x19);
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

// Agarrar el cursor de la barra y arrastrarlo arriba del todo va al final de la cuenta.
TEST_F(OldListTest, DraggingTheThumbToTheTopScrollsToTheEnd)
{
    Client::UI::PlaceTheListBar(m_list, 20);
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

// 0x47b714: los renglones desde el corrimiento, hasta los renglones o el final; con una caja modal, ninguno.
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

// 0x461c70: el primero que se ve va abajo y los demás suben de a 13.
TEST_F(OldListTest, TheFirstShownRowIsTheLowest)
{
    EXPECT_EQ(Client::UI::ListRowY(m_list, 20, 0), 6 * 13 - 0x5b - 10 + 300);
    EXPECT_EQ(Client::UI::ListRowY(m_list, 20, 5), 13 - 0x5b - 10 + 300);
    EXPECT_EQ(Client::UI::ListRowY(m_list, 2, 0), 2 * 13 - 0x5b - 10 + 300);
}
} // namespace
