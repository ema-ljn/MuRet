#include "UI/NoticeWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "UI/Notices.h"
#include "UI/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <optional>

// Client::UI::NoticeWindow: el aviso de la UI nueva del tipo 2, con un OK (0x6429b0, 0x642bf0,
// 0x642f00 y 0x642e30, en el paso; CL5.5). Oscurece la pantalla, muestra su caja con el texto por
// índice y se cierra con OK, Enter o Esc. El nombre no es MessageBox: <windows.h> define una macro
// con ese nombre.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::TapKey;
using Client::UI::LayoutNotice;
using Client::UI::NoticeId;
using Client::UI::NoticeLayout;
using Client::UI::NoticeWindow;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};

Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0x727, {512, 128}}, {0x724, {64, 128}}});
}

std::optional<NoticeId> Some(NoticeId notice)
{
    return notice;
}

std::optional<NoticeId> StepOnce(NoticeWindow& window, InputState& input)
{
    std::optional<NoticeId> closed = window.Step(input, Screen640);
    input.ConsumeEdges();
    return closed;
}

// La caja de 352 × 113 al medio, ((W − 352) / 2, (H − 113) / 2), y el OK de 54 × 30 en (+149, +72).
TEST(NoticeWindowTest, TheBoxIsCenteredWithItsOk)
{
    const NoticeLayout layout = LayoutNotice(Screen640);

    EXPECT_EQ(layout.box, (Rect{.x = 144, .y = 183, .width = 352, .height = 113}));
    EXPECT_EQ(layout.ok, (Rect{.x = 293, .y = 255, .width = 54, .height = 30}));
}

TEST(NoticeWindowTest, OkClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x70);
    const NoticeLayout layout = LayoutNotice(Screen640);
    InputState input;
    ClickAt(input, layout.ok.x + 10, layout.ok.y + 10);

    EXPECT_EQ(StepOnce(window, input), Some(0x70));
    EXPECT_FALSE(window.IsOpen());
    EXPECT_EQ(window.Notice(), std::nullopt);
}

// 0x642e30, el tipo 2: Enter o Esc son el OK.
TEST(NoticeWindowTest, EnterOrEscClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    InputState input;
    window.Open(0x64);
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), Some(0x64));

    window.Open(0x65);
    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), Some(0x65));
}

// Un clic en otro lado no lo cierra, y cerrado no hace nada.
TEST(NoticeWindowTest, OnlyItsOkClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    InputState input;
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);

    window.Open(0x70);
    EXPECT_EQ(window.Notice(), Some(0x70));
    ClickAt(input, 5, 5);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// 0x6429b0: la base es una imagen sin textura (slot -1) de toda la pantalla, negra con alfa 0x80;
// después la caja (message_back, slot 0x727) y el OK (0x724). El lote llega con la aditiva
// (Q-C80): la ventana pone la normal.
TEST(NoticeWindowTest, TheBaseDarkensTheScreen)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x70);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(Client::Render::BlendMode::Additive);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    const auto commands = batch.Commands();
    ASSERT_GE(commands.size(), 3u);
    EXPECT_EQ(commands[0].texture, std::nullopt);
    EXPECT_EQ(commands[0].blend, Client::Render::BlendMode::Alpha);
    EXPECT_EQ(commands[1].texture, Client::Render::TextureSlot{0x727});
    EXPECT_EQ(commands[2].texture, Client::Render::TextureSlot{0x724});
    const auto base = batch.Vertices().first(6);
    EXPECT_EQ(base[0].x, 0.0f);
    EXPECT_EQ(base[0].y, 480.0f);
    EXPECT_EQ(base[2].x, 640.0f);
    EXPECT_EQ(base[2].y, 0.0f);
    EXPECT_EQ(base[0].color.red, 0.0f);
    EXPECT_FLOAT_EQ(base[0].color.alpha, 128.0f / 255.0f);
}

// 0x642bf0: una línea va centrada en el ancho de la caja, en la y + 38, con la fuente
// DAT_05763560 en blanco. "Synthetic" mide 9 · 6 = 54: (352 − 54) / 2 = 149.
TEST(NoticeWindowTest, OneLineIsCentered)
{
    const auto texts = Client::Tests::MakeTextTable({{402, "Synthetic"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x70);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Synthetic");
    EXPECT_EQ(text.texts[0].x, 144 + 149);
    EXPECT_EQ(text.texts[0].y, 183 + 38);
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::OtherFace12);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
}

// Dos líneas (el 0x6f, la versión: 405 y 406) van a la izquierda, en (+25, +32) y (+25, +51).
TEST(NoticeWindowTest, TwoLinesGoOnTheLeft)
{
    const auto texts = Client::Tests::MakeTextTable({{405, "First"}, {406, "Second"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x6f);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "First");
    EXPECT_EQ(text.texts[0].x, 169);
    EXPECT_EQ(text.texts[0].y, 215);
    EXPECT_EQ(text.texts[1].text, "Second");
    EXPECT_EQ(text.texts[1].x, 169);
    EXPECT_EQ(text.texts[1].y, 234);
}
} // namespace
