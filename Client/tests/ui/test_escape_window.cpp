#include "UI/EscapeWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

// Client::UI::EscapeWindow: la ventana de Esc del login y de la selección (+0x3d0: 0x6478c0, 0x647c00,
// 0x647cd0, 0x647d40 y 0x647ed0; CL8, hito 8). Todo en píxeles.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::UI::EscapeWindow;
using Client::UI::EscapeWindowEvent;
using Client::UI::EscapeWindowLayout;
using Client::UI::LayoutEscapeWindow;
using Client::UI::NewUiMode;
using Client::UI::NewUiTurn;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};
// Un paso de la lógica: 40 ms (App::LogicStep).
constexpr double StepMs = 40.0;
constexpr std::size_t ExitButton = 0;
constexpr std::size_t ServerButton = 1;
constexpr std::size_t OptionsButton = 2;
constexpr std::size_t CloseButton = 3;

// Los buffers del laboratorio: op1_back3 de 5 × 8 sube en 8 × 8.
Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0x72a, {128, 128}},
                                            {0x72b, {256, 64}},
                                            {0x72c, {256, 64}},
                                            {0x72d, {8, 8}},
                                            {0x72e, {8, 8}},
                                            {0x72f, {128, 128}}});
}

EscapeWindow OpenWindow(NewUiMode mode)
{
    EscapeWindow window(LabSizes(), nullptr);
    window.Arm(mode);
    window.Open();
    // Un paso sin nada la ubica (0x647c00).
    const InputState nothing;
    static_cast<void>(window.Step(nothing, Screen640, 0.0));
    return window;
}

std::optional<EscapeWindowEvent> StepOnce(EscapeWindow& window, InputState& input, double elapsedMs = StepMs)
{
    std::optional<EscapeWindowEvent> event = window.Step(input, Screen640, elapsedMs);
    input.ConsumeEdges();
    return event;
}

std::optional<EscapeWindowEvent> ClickOn(EscapeWindow& window, InputState& input, std::size_t button)
{
    const Rect area = window.ButtonAt(button).Area();
    Client::Tests::ClickAt(input, area.x + 5, area.y + 5);
    return StepOnce(window, input);
}

// 0x647c00 con 6 líneas de 8 (el login): el marco de 213 × 155 al centro, los botones a (213 − 108) / 2 del
// borde, el primero a 33 de arriba, el segundo 34 más abajo, el último a 52 del borde de abajo y el de las
// opciones 34 más arriba.
TEST(EscapeWindowTest, TheLoginFrameHasSixLines)
{
    const EscapeWindowLayout layout = LayoutEscapeWindow(Screen640, 6, 8);

    EXPECT_EQ(layout.frame, (Rect{213, 162, 213, 155}));
    EXPECT_EQ(layout.buttons[ExitButton], (Rect{265, 195, 108, 30}));
    EXPECT_EQ(layout.buttons[ServerButton], (Rect{265, 229, 108, 30}));
    EXPECT_EQ(layout.buttons[OptionsButton], (Rect{265, 231, 108, 30}));
    EXPECT_EQ(layout.buttons[CloseButton], (Rect{265, 265, 108, 30}));
}

TEST(EscapeWindowTest, TheSelectionFrameHasTen)
{
    const EscapeWindowLayout layout = LayoutEscapeWindow(Screen640, 10, 8);

    EXPECT_EQ(layout.frame, (Rect{213, 146, 213, 187}));
    EXPECT_EQ(layout.buttons[ServerButton], (Rect{265, 213, 108, 30}));
    EXPECT_EQ(layout.buttons[OptionsButton], (Rect{265, 247, 108, 30}));
    EXPECT_EQ(layout.buttons[CloseButton], (Rect{265, 281, 108, 30}));
}

// 0x66a960: la piedra 3 adentro, los bordes de 64 y 43, y los lados de 5 con las líneas en el medio. La
// piedra y los lados repiten su textura (0x647370).
TEST(EscapeWindowTest, TheFrameIsFiveImages)
{
    const auto images = Client::UI::EscapeFrameImages(Rect{213, 162, 213, 155}, LabSizes());

    EXPECT_EQ(images[0].slot, Client::Render::TextureSlot{0x72a});
    EXPECT_EQ(images[0].x, 216);
    EXPECT_EQ(images[0].y, 165);
    EXPECT_EQ(images[0].width, 207);
    EXPECT_EQ(images[0].height, 149);
    EXPECT_TRUE(images[0].resizedWidth);
    EXPECT_TRUE(images[0].resizedHeight);
    EXPECT_EQ(images[1].slot, Client::Render::TextureSlot{0x72b});
    EXPECT_EQ(images[1].y, 162);
    EXPECT_EQ(images[1].height, 64);
    EXPECT_EQ(images[2].slot, Client::Render::TextureSlot{0x72c});
    EXPECT_EQ(images[2].y, 274);
    EXPECT_EQ(images[2].height, 43);
    EXPECT_EQ(images[3].x, 213);
    EXPECT_EQ(images[3].y, 226);
    EXPECT_EQ(images[3].height, 48);
    EXPECT_TRUE(images[3].resizedHeight);
    EXPECT_EQ(images[4].slot, Client::Render::TextureSlot{0x72e});
    EXPECT_EQ(images[4].x, 421);
    EXPECT_EQ(images[4].width, 5);
}

// 0x6478c0: elegir otro servidor solo en la selección. En el login, el de las opciones le tapa casi todo
// el lugar (de 231 a 260): el clic va en las dos filas de arriba.
TEST(EscapeWindowTest, OnlyTheSelectionCanJoinAnotherServer)
{
    EscapeWindow login = OpenWindow(NewUiMode::Login);
    EscapeWindow selection = OpenWindow(NewUiMode::Selection);
    InputState input;

    EXPECT_FALSE(login.ButtonAt(ServerButton).Enabled());
    const Rect server = login.ButtonAt(ServerButton).Area();
    Client::Tests::ClickAt(input, server.x + 5, server.y + 1);
    EXPECT_EQ(StepOnce(login, input), std::nullopt);
    EXPECT_TRUE(login.IsOpen());

    EXPECT_TRUE(selection.ButtonAt(ServerButton).Enabled());
    EXPECT_EQ(ClickOn(selection, input, ServerButton),
              (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::JoinAnotherServer}));
    EXPECT_FALSE(selection.IsOpen());
}

TEST(EscapeWindowTest, TheOptionsAndTheCloseButtonsClose)
{
    InputState input;
    EscapeWindow options = OpenWindow(NewUiMode::Login);
    EXPECT_EQ(ClickOn(options, input, OptionsButton), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Options}));
    EXPECT_FALSE(options.IsOpen());

    EscapeWindow close = OpenWindow(NewUiMode::Login);
    EXPECT_EQ(ClickOn(close, input, CloseButton), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Closed}));
    EXPECT_FALSE(close.IsOpen());
}

// Esc con 0x8001 la cierra y se lleva el apretón: el paso de la escena ya no lo ve (Q-C45).
TEST(EscapeWindowTest, EscapeClosesItAndTakesThePress)
{
    EscapeWindow window = OpenWindow(NewUiMode::Selection);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Closed}));
    EXPECT_FALSE(window.IsOpen());
    EXPECT_FALSE(input.TakePress(Key::Escape));
}

// Sin el foco, 0x669f40 no corre 0x647d40: ni Esc ni clics.
TEST(EscapeWindowTest, WithoutTheFocusNothingHappens)
{
    EscapeWindow window = OpenWindow(NewUiMode::Selection);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs, NewUiTurn{.mouse = false, .logic = false}), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_TRUE(input.TakePress(Key::Escape));
}

// 0x647d40: salir arranca la cuenta en 5 (con el 380), y cada vez que el acumulador pasa de 1.000 ms baja
// uno y lo vuelve a 0. A 40 ms, 25 pasos suman 1.000 justo, que no pasa: baja en el 26. Con la cuenta, Esc
// no la cierra. En 0, WM_CLOSE.
TEST(EscapeWindowTest, LeavingCountsFiveSeconds)
{
    EscapeWindow window = OpenWindow(NewUiMode::Login);
    InputState input;

    EXPECT_EQ(ClickOn(window, input, ExitButton),
              (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Countdown, .seconds = 5}));
    EXPECT_EQ(window.Countdown(), 5);
    for (int step = 1; step < 25; ++step)
    {
        ASSERT_EQ(StepOnce(window, input), std::nullopt) << "paso " << step;
    }
    Client::Tests::PressKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_EQ(StepOnce(window, input), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Countdown, .seconds = 4}));
    for (int seconds = 3; seconds > 0; --seconds)
    {
        EXPECT_EQ(StepOnce(window, input, 1001.0),
                  (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Countdown, .seconds = seconds}));
    }
    EXPECT_EQ(StepOnce(window, input, 1001.0), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Quit}));
    EXPECT_EQ(window.Countdown(), 0);
}

// 0x66a050 y 0x647ed0: la base negra, las cinco del marco y los botones; en el login el 1, deshabilitado y
// con −1 de cuadro deshabilitado, no se dibuja (0x63c710). Los rótulos, con el texto de cada uno (el que
// los graba suma un quad por rótulo).
TEST(EscapeWindowTest, TheLoginDoesNotDrawTheServerButton)
{
    const auto texts = Client::Tests::MakeTextTable({{381, "Uno"}, {382, "Dos"}, {385, "Tres"}, {388, "Cuatro"}});
    EscapeWindow window(LabSizes(), &texts);
    window.Arm(NewUiMode::Login);
    window.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    std::size_t quads = 0;
    for (const auto& command : batch.Commands())
    {
        quads += command.vertexCount / 6;
    }
    EXPECT_EQ(quads, 1U + 5U + 3U + 3U);
    ASSERT_EQ(text.texts.size(), 3U);
    EXPECT_EQ(text.texts[0].text, "Uno");
    EXPECT_EQ(text.texts[1].text, "Tres");
    EXPECT_EQ(text.texts[2].text, "Cuatro");
}
} // namespace
