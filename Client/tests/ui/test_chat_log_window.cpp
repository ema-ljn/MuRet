#include "UI/ChatLogWindow.h"

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatLog.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// El paso del registro del chat (CL8 hito 3; 06 § El chat): 0x45aa80 y 0x479960 con 0x45d760, 0x45d990, 0x45dec0 y la
// barra de 0x47a230. Los textos son inventados.
namespace
{
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::UI::ChatEntry;
using Client::UI::ChatLogFrame;
using Client::UI::ChatLogGlobals;
using Client::UI::ChatLogId;
using Client::UI::ChatLogState;
using Client::UI::ChatLogStepResult;
using Client::UI::ChatLogStepView;
using Client::UI::ChatModes;

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
        return Client::UI::StepTheChatLog(m_log, m_frame, globals, view);
    }
};

// La lista ocupa (x, y − h, w, h) desde su borde de abajo: con el mouse encima es la de abajo del mouse.
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

// 0x479960: sobre la barra, la rueda corre 3 renglones por paso de la rueda y se apaga.
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

// La flecha de arriba (x + w − 0x15, y − h + 8, 13, 13): un renglón y el 0x19 al apretarla; después cuenta hasta 16
// pasos y desde ahí corre en cada uno.
TEST_F(ChatLogWindowTest, TheUpArrowScrollsOnceAndRepeatsAfter16Steps)
{
    Fill(40);
    m_leftClick = true;

    const ChatLogStepResult first = Step(435, 180);
    EXPECT_EQ(m_log.scroll, 1);
    EXPECT_EQ(first.sounds, std::vector<std::int32_t>{Client::UI::ChatButtonSound});
    EXPECT_EQ(m_focused, ChatLogId);

    for (int step = 0; step < 15; ++step)
        static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.scroll, 1);
    EXPECT_EQ(m_log.arrowRepeat, 16);

    static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.scroll, 2);

    // Sin el clic, las cuentas vuelven a 0.
    m_leftClick = false;
    static_cast<void>(Step(435, 180));
    EXPECT_EQ(m_log.arrowRepeat, 0);
}

// 0x45d760: el clic derecho sobre el elegido lo manda al campo del susurro; la elección vuelve a 0 en cada paso.
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

// 0x45dec0: con la zona segura y el foco en el chat, el mouse sobre el nombre de otro lo elige (scroll + 1 + renglón).
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

    // El renglón 1 (y = 0x65 − 13 − 16): el nombre del héroe no se elige.
    static_cast<void>(Step(5, 75, view));
    EXPECT_EQ(m_log.selection, 0);

    // El renglón 0, "Ana" de 18 píxeles desde x = 0.
    static_cast<void>(Step(5, 88, view));
    EXPECT_EQ(m_log.selection, 1);
    EXPECT_EQ(m_log.entries[0].blink, 1);

    // Sin el foco en el chat, nada.
    view.chatEditFocused = false;
    static_cast<void>(Step(5, 88, view));
    EXPECT_EQ(m_log.selection, 0);
}

// El borde de arriba (x, y − h, w, 8) se agarra con el clic; el alto sigue al mouse de 40 en 40 (3 renglones) y el
// clic suelto lo deja.
TEST_F(ChatLogWindowTest, TheTopEdgeResizesTheListInStepsOfThreeRows)
{
    Fill(20);
    m_leftClick = true;
    // Sobre la barra (para que 0x45d760 dé verdadero) y sobre el borde.
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

// 0x47a230: la pista de y − h + 21 a y − 17, el cursor del tamaño de rows / visibles y su lugar por el corrimiento.
TEST_F(ChatLogWindowTest, TheBarFollowsTheRowsAndTheScroll)
{
    Fill(12);
    m_log.scroll = 3;

    Client::UI::PlaceTheLogBar(m_log, m_modes, m_frame);

    EXPECT_FLOAT_EQ(m_frame.barTop, 191.0f);
    EXPECT_FLOAT_EQ(m_frame.barBottom, 403.0f);
    EXPECT_FLOAT_EQ(m_frame.thumbHeight, 106.0f);
    EXPECT_FLOAT_EQ(m_frame.thumbY, 244.0f);
}

// Arrastrar el cursor: lo agarra con el clic y el corrimiento sigue su lugar; suelto, la lista queda quieta.
TEST_F(ChatLogWindowTest, DraggingTheThumbScrollsAndTheReleasedClickStopsIt)
{
    Fill(12);
    Client::UI::PlaceTheLogBar(m_log, m_modes, m_frame);
    m_leftClick = true;
    // Sin correr, el cursor está abajo: (x + w − 0x13, 297, 13, 106).
    static_cast<void>(Step(440, 360));
    EXPECT_EQ(m_log.state, 2);
    EXPECT_EQ(m_capture, ChatLogId);
    EXPECT_FLOAT_EQ(m_log.dragOffset, 63.0f);

    // Arriba del todo: los más viejos.
    static_cast<void>(Step(440, 100));
    EXPECT_EQ(m_log.scroll, 6);
    // Abajo: los nuevos.
    static_cast<void>(Step(440, 400));
    EXPECT_EQ(m_log.scroll, 0);

    m_leftClick = false;
    static_cast<void>(Step(440, 100));
    EXPECT_EQ(m_log.state, 0);
    EXPECT_EQ(m_capture, 0);
    EXPECT_EQ(m_log.scroll, 0);
}

// 0x45d990: los botones, con el mouse fuera de toda ventana. El tercero sube la transparencia de a 0,2 y pasada 0,9
// vuelve a 0,2; el primero es F2.
TEST_F(ChatLogWindowTest, TheButtonsToggleF2AndCycleTheTransparency)
{
    m_log.buttonsShown = 1;
    m_leftClick = true;

    ChatLogStepResult result = Step(335, 425);
    EXPECT_FLOAT_EQ(m_log.alpha, 0.6f);
    EXPECT_FALSE(m_leftClick);
    EXPECT_EQ(result.sounds, std::vector<std::int32_t>{Client::UI::ChatButtonSound});

    m_leftClick = true;
    static_cast<void>(Step(335, 425));
    m_leftClick = true;
    static_cast<void>(Step(335, 425));
    EXPECT_FLOAT_EQ(m_log.alpha, 0.2f);

    m_leftClick = true;
    static_cast<void>(Step(300, 425));
    EXPECT_FALSE(m_modes.showChat);
}

// 0x45d760: los botones aparecen con el mouse en (0xa0, 0x1b4, 0x140, 0x32) y se van con el mouse arriba de 0x1a0.
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
