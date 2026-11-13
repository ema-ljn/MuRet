#include "UI/PanelDialog.h"

#include "Core/Expected.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "UI/ChatInput.h"
#include "UI/OldWindows.h"
#include "UI/Widget.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El diálogo de los paneles (CL9 hito 8; 06 § El diálogo de los paneles): 0x6600d0, 0x660350, 0x6603c0, 0x660410,
// 0x660580, 0x6605b0 y la parte de 0x660830 que ubica los botones. Los renglones son inventados.
namespace
{
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::UI::OldWindowGlobals;
using Client::UI::PanelDialogOpening;
using Client::UI::PanelDialogOwner;
using Client::UI::PanelDialogRequest;
using Client::UI::PanelDialogState;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

constexpr std::uint32_t OpenedAt = 0x1234;

class PanelDialogTest : public ::testing::Test
{
protected:
    PanelDialogOpening Open(std::uint32_t flags, std::vector<std::string> lines = {"Un pedido", "", "Acepta?"},
                            PanelDialogOwner owner = PanelDialogOwner::GuildJoin, std::uint32_t now = OpenedAt)
    {
        m_lines = std::move(lines);
        return Client::UI::OpenThePanelDialog(
            m_dialog, PanelDialogRequest{.lines = m_lines, .flags = flags, .owner = owner, .placed = 0}, m_letters,
            FontKind::Normal, now);
    }

    Client::UI::PanelDialogAnswer Recorder(bool closes = true)
    {
        return [this, closes](PanelDialogOwner owner, std::uint32_t reason) -> Client::Core::Expected<bool, std::string>
        {
            m_answers.emplace_back(owner, reason);
            return closes;
        };
    }

    OldWindowGlobals Globals()
    {
        return OldWindowGlobals{.leftHeld = m_leftHeld,
                                .leftClick = m_leftClick,
                                .releasedClick = m_released,
                                .mouseOnUi = m_mouseOnUi,
                                .hovered = m_hovered,
                                .capture = m_capture,
                                .pressedButton = m_pressedButton,
                                .keyboardFocus = m_focus,
                                .sound = m_sound,
                                .focusedList = m_focusedList,
                                .wheel = m_wheel,
                                .rightClick = m_rightClick,
                                .doubleClick = m_doubleClick};
    }

    PanelDialogState m_dialog;
    LetterMeasure m_letters;
    std::vector<std::string> m_lines;
    std::vector<std::pair<PanelDialogOwner, std::uint32_t>> m_answers;
    bool m_leftHeld = false;
    bool m_leftClick = false;
    bool m_released = false;
    std::uint8_t m_mouseOnUi = 0;
    std::int32_t m_hovered = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_pressedButton = 0;
    Client::UI::ChatFocus m_focus{};
    std::optional<std::int32_t> m_sound;
    std::int32_t m_focusedList = 0;
    std::int32_t m_wheel = 0;
    bool m_rightClick = false;
    bool m_doubleClick = false;
};

// 0x6600d0: el alto suma cy + 2 de cada renglón no vacío, 0x3f con botones (0x2b sin ellos) y 0x11 por el campo y por
// el reloj; el ancho es 0xd5 y el reloj termina 5.000 ms después.
TEST_F(PanelDialogTest, OpeningMeasuresTheLinesAndStartsTheTimer)
{
    ASSERT_EQ(Open(Client::UI::PanelDialogConfirm), PanelDialogOpening::Opened);
    EXPECT_EQ(m_dialog.opened, OpenedAt);
    EXPECT_EQ(Client::UI::PanelDialogHandle(m_dialog), OpenedAt);
    EXPECT_EQ(m_dialog.lineCount, 3);
    EXPECT_EQ(m_dialog.lines[0], "Un pedido");
    EXPECT_EQ(m_dialog.width, 0xd5);
    EXPECT_EQ(m_dialog.height, 2 * (13 + 2) + 0x3f);
    EXPECT_EQ(m_dialog.timerEnd, OpenedAt + 5000);
    EXPECT_EQ(m_dialog.owner, PanelDialogOwner::GuildJoin);

    m_dialog = PanelDialogState{};
    ASSERT_EQ(Open(Client::UI::PanelDialogTimed | Client::UI::PanelDialogTextField), PanelDialogOpening::Opened);
    EXPECT_EQ(m_dialog.height, 2 * (13 + 2) + 0x2b + 0x11 + 0x11);
}

// 0x6600d6 y 0x6600f9: más de diez renglones u otro abierto, no abre.
TEST_F(PanelDialogTest, ItDoesNotOpenWithTooManyLinesOrWhenBusy)
{
    EXPECT_EQ(Open(Client::UI::PanelDialogOk, std::vector<std::string>(11, "x")), PanelDialogOpening::TooManyLines);
    EXPECT_EQ(m_dialog.opened, 0u);
    ASSERT_EQ(Open(Client::UI::PanelDialogOk), PanelDialogOpening::Opened);
    EXPECT_EQ(Open(Client::UI::PanelDialogOk, {"otro"}, PanelDialogOwner::Relationship, 99), PanelDialogOpening::Busy);
    EXPECT_EQ(m_dialog.opened, OpenedAt);
    EXPECT_EQ(m_dialog.owner, PanelDialogOwner::GuildJoin);
}

// 0x660410: Esc responde según los botones (0x50 con Confirmar) y se cierra; Enter sin el campo no es suyo; con la
// función en 0 queda abierto.
TEST_F(PanelDialogTest, TheKeysAnswerByTheButtons)
{
    ASSERT_EQ(Open(Client::UI::PanelDialogConfirm), PanelDialogOpening::Opened);
    auto enter = Client::UI::ThePanelDialogKey(m_dialog, 0x0d, Recorder());
    ASSERT_TRUE(enter.HasValue());
    EXPECT_FALSE(enter.Value());
    EXPECT_TRUE(m_answers.empty());

    auto kept = Client::UI::ThePanelDialogKey(m_dialog, 0x1b, Recorder(false));
    ASSERT_TRUE(kept.HasValue());
    EXPECT_TRUE(kept.Value());
    EXPECT_NE(m_dialog.opened, 0u);

    auto escape = Client::UI::ThePanelDialogKey(m_dialog, 0x1b, Recorder());
    ASSERT_TRUE(escape.HasValue());
    EXPECT_TRUE(escape.Value());
    EXPECT_EQ(m_dialog.opened, 0u);
    EXPECT_EQ(m_dialog.owner, PanelDialogOwner::None);
    const std::vector<std::pair<PanelDialogOwner, std::uint32_t>> expected{{PanelDialogOwner::GuildJoin, 0x50},
                                                                           {PanelDialogOwner::GuildJoin, 0x50}};
    EXPECT_EQ(m_answers, expected);

    // Cerrado, 0x660410 da 0.
    auto closed = Client::UI::ThePanelDialogKey(m_dialog, 0x1b, Recorder());
    ASSERT_TRUE(closed.HasValue());
    EXPECT_FALSE(closed.Value());
}

// 0x6603c0: el motivo de cancelar de cada botón, sin mirar lo que devuelve la función.
TEST_F(PanelDialogTest, CancellingAnswersWithTheCancelReason)
{
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> cases{{{Client::UI::PanelDialogOk, 2},
                                                                        {Client::UI::PanelDialogYesNo, 4},
                                                                        {Client::UI::PanelDialogConfirm, 0x10},
                                                                        {Client::UI::PanelDialogTimed, 1}}};
    for (const auto& [flags, reason] : cases)
    {
        SCOPED_TRACE(flags);
        m_answers.clear();
        ASSERT_EQ(Open(flags), PanelDialogOpening::Opened);
        ASSERT_TRUE(Client::UI::CancelThePanelDialog(m_dialog, Recorder(false)).HasValue());
        ASSERT_EQ(m_answers.size(), 1u);
        EXPECT_EQ(m_answers[0].second, reason);
        EXPECT_EQ(m_dialog.opened, 0u);
    }
    // Sin función (el que abrió sin ella), cierra sin preguntar.
    ASSERT_EQ(Open(Client::UI::PanelDialogOk, {"x"}, PanelDialogOwner::None), PanelDialogOpening::Opened);
    ASSERT_TRUE(Client::UI::CancelThePanelDialog(m_dialog, {}).HasValue());
    EXPECT_EQ(m_dialog.opened, 0u);
}

// 0x660830 ubica los botones y 0x6605b0 los aprieta: el clic sobre Confirmar y el soltado responden 8.
TEST_F(PanelDialogTest, TheStepClicksTheConfirmButton)
{
    ASSERT_EQ(Open(Client::UI::PanelDialogConfirm), PanelDialogOpening::Opened);
    const Client::UI::Point box = Client::UI::PlaceThePanelDialog(m_dialog);
    // Alto 0x5d: 0x82 − 0x2e = 0x54; los botones en 0x54 + 0x14 + 45 + 9.
    EXPECT_EQ(box.x, 0xd6);
    EXPECT_EQ(box.y, 0x54);
    const auto& confirm = m_dialog.buttons[Client::UI::PanelConfirmButton].base;
    EXPECT_EQ(confirm.x, 0x104);
    EXPECT_EQ(confirm.y, 0x54 + 0x14 + 45 + 9);
    EXPECT_EQ(m_dialog.buttons[Client::UI::PanelCancelButton].base.x, 0x14a);

    const std::int32_t x = confirm.x + 5;
    const std::int32_t y = confirm.y + 5;
    m_leftClick = true;
    ASSERT_TRUE(Client::UI::StepThePanelDialog(m_dialog, Globals(), x, y, OpenedAt + 10, Recorder()).HasValue());
    EXPECT_EQ(m_mouseOnUi, 1);
    EXPECT_TRUE(m_answers.empty());
    m_leftClick = false;
    m_released = true;
    ASSERT_TRUE(Client::UI::StepThePanelDialog(m_dialog, Globals(), x, y, OpenedAt + 20, Recorder()).HasValue());
    ASSERT_EQ(m_answers.size(), 1u);
    EXPECT_EQ(m_answers[0].second, 8u);
    EXPECT_EQ(m_sound, std::optional<std::int32_t>(0x19));
    EXPECT_EQ(m_dialog.opened, 0u);
}

// 0x660580: con el reloj (8), al pasar el final responde 0x20; sin él, nunca.
TEST_F(PanelDialogTest, TheTimerAnswersWhenItRunsOut)
{
    ASSERT_EQ(Open(Client::UI::PanelDialogConfirm), PanelDialogOpening::Opened);
    ASSERT_TRUE(Client::UI::StepThePanelDialog(m_dialog, Globals(), 0, 0, OpenedAt + 6000, Recorder()).HasValue());
    EXPECT_TRUE(m_answers.empty());
    ASSERT_TRUE(Client::UI::CancelThePanelDialog(m_dialog, Recorder()).HasValue());
    m_answers.clear();

    ASSERT_EQ(Open(Client::UI::PanelDialogConfirm | Client::UI::PanelDialogTimed), PanelDialogOpening::Opened);
    ASSERT_TRUE(Client::UI::StepThePanelDialog(m_dialog, Globals(), 0, 0, OpenedAt + 4999, Recorder()).HasValue());
    EXPECT_TRUE(m_answers.empty());
    ASSERT_TRUE(Client::UI::StepThePanelDialog(m_dialog, Globals(), 0, 0, OpenedAt + 5000, Recorder()).HasValue());
    ASSERT_EQ(m_answers.size(), 1u);
    EXPECT_EQ(m_answers[0].second, 0x20u);
    EXPECT_EQ(m_dialog.opened, 0u);
}

// 0x660350: el campo de texto vacía el EDIT o el buffer del chat, que MuRet no abre: corta.
TEST_F(PanelDialogTest, ClosingWithTheTextFieldIsCut)
{
    ASSERT_EQ(Open(Client::UI::PanelDialogTextField, {"x"}, PanelDialogOwner::None), PanelDialogOpening::Opened);
    EXPECT_FALSE(Client::UI::CloseThePanelDialog(m_dialog).HasValue());
}
} // namespace
