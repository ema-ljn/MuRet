#include "FrontEnd/EscapeWindow.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>

// Client::FrontEnd::EscapeWindow: the Esc window of the login and the selection (+0x3d0: 0x6478c0, 0x647c00,
// 0x647cd0, 0x647d40 and 0x647ed0; CL8, milestone 8). All in pixels.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::EscapeWindow;
using Client::FrontEnd::EscapeWindowEvent;
using Client::FrontEnd::EscapeWindowLayout;
using Client::FrontEnd::LayoutEscapeWindow;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Ui::NewUiMode;
using Client::Ui::NewUiTurn;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
// One logic step: 40 ms (Kernel::LogicStep).
constexpr double StepMs = 40.0;
constexpr std::size_t ExitButton = 0;
constexpr std::size_t ServerButton = 1;
constexpr std::size_t OptionsButton = 2;
constexpr std::size_t CloseButton = 3;

// The lab's buffers: op1_back3, 5 × 8, is uploaded in 8 × 8.
Client::Ui::TextureSizes LabSizes()
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
    // A step with nothing places it (0x647c00).
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

// 0x647c00 with 6 lines of 8 (the login): the 213 × 155 frame in the center, the buttons at (213 − 108) / 2 from the
// edge, the first 33 from the top, the second 34 lower, the last 52 from the bottom edge and the options one 34
// higher.
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

// 0x66a960: stone 3 inside, the edges of 64 and 43, and the sides of 5 with the lines in the middle. The stone and
// the sides repeat their texture (0x647370).
TEST(EscapeWindowTest, TheFrameIsFiveImages)
{
    const auto images = Client::FrontEnd::EscapeFrameImages(Rect{213, 162, 213, 155}, LabSizes());

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

// 0x6478c0: choosing another server only in the selection. In the login, the options one covers almost all its place
// (from 231 to 260): the click goes in the top two rows.
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

// Esc with 0x8001 closes it and takes the press: the scene's step no longer sees it (Q-C45).
TEST(EscapeWindowTest, EscapeClosesItAndTakesThePress)
{
    EscapeWindow window = OpenWindow(NewUiMode::Selection);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs), (EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Closed}));
    EXPECT_FALSE(window.IsOpen());
    EXPECT_FALSE(input.TakePress(Key::Escape));
}

// Without the focus, 0x669f40 does not run 0x647d40: neither Esc nor clicks.
TEST(EscapeWindowTest, WithoutTheFocusNothingHappens)
{
    EscapeWindow window = OpenWindow(NewUiMode::Selection);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs, NewUiTurn{.mouse = false, .logic = false}), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_TRUE(input.TakePress(Key::Escape));
}

// 0x647d40: leaving starts the count at 5 (with the 380), and each time the accumulator passes 1000 ms it goes down
// one and resets it to 0. At 40 ms, 25 steps add up to exactly 1000, which does not pass: it goes down on the 26th.
// With the count, Esc does not close it. At 0, WM_CLOSE.
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

// 0x66a050 and 0x647ed0: the black base, the frame's five and the buttons; in the login button 1, disabled and with a
// disabled frame of −1, is not drawn (0x63c710). The labels, with each one's text (the recorder adds a quad per
// label).
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
