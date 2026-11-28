#include "FrontEnd/OptionsWindow.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <optional>

// Client::FrontEnd::OptionsWindow: the options of the login and the selection (+0x7e8: 0x644120, 0x644520, 0x644640,
// 0x644730 and 0x644860; CL8, milestone 8). All in pixels.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::LayoutOptionsWindow;
using Client::FrontEnd::OptionsEvent;
using Client::FrontEnd::OptionsSettings;
using Client::FrontEnd::OptionsWindow;
using Client::FrontEnd::OptionsWindowLayout;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Ui::NewUiTurn;
using Client::Ui::Point;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
// One logic step: 40 ms (Kernel::LogicStep).
constexpr double StepMs = 40.0;

// The lab's buffers: op1_back3, 5 × 8, is uploaded in 8 × 8.
Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0x72a, {128, 128}},
                                            {0x72c, {256, 64}},
                                            {0x72d, {8, 8}},
                                            {0x72e, {8, 8}},
                                            {0x72f, {128, 128}},
                                            {0x730, {256, 128}},
                                            {0x731, {16, 32}}});
}

OptionsWindow OpenWindow(const OptionsSettings& settings)
{
    OptionsWindow window(LabSizes(), nullptr);
    window.Arm(settings);
    window.Open();
    // A step with nothing places it (0x644520).
    const InputState nothing;
    static_cast<void>(window.Step(nothing, Screen640, 0.0, settings));
    return window;
}

std::optional<OptionsEvent> StepOnce(OptionsWindow& window, InputState& input, const OptionsSettings& current)
{
    std::optional<OptionsEvent> event = window.Step(input, Screen640, StepMs, current);
    input.ConsumeEdges();
    return event;
}

std::optional<OptionsEvent> ClickOn(OptionsWindow& window, InputState& input, Rect area, const OptionsSettings& current)
{
    Client::Tests::ClickAt(input, area.x + 5, area.y + 5);
    return StepOnce(window, input, current);
}

// 0x644520 with 30 lines of 8: the 213 × 348 frame in the center; the checkboxes 52 from the edge, one every 41; the
// volume 48 below the last plus its height, the glow 61 lower, and the button 301 from the top.
TEST(OptionsWindowTest, TheFrameHasThirtyLines)
{
    const OptionsWindowLayout layout = LayoutOptionsWindow(Screen640, 8);

    EXPECT_EQ(layout.frame, (Rect{213, 66, 213, 348}));
    EXPECT_EQ(layout.checks[0], (Rect{265, 118, 16, 16}));
    EXPECT_EQ(layout.checks[1], (Rect{265, 159, 16, 16}));
    EXPECT_EQ(layout.checks[2], (Rect{265, 200, 16, 16}));
    EXPECT_EQ(layout.sliders[0], (Point{265, 264}));
    EXPECT_EQ(layout.sliders[1], (Point{265, 325}));
    EXPECT_EQ(layout.close, (Rect{265, 367, 108, 30}));
}

// 0x6446e0: the checkboxes and the sliders with the globals; the volume up to 9 and the glow up to 4.
TEST(OptionsWindowTest, ArmingReadsTheGlobals)
{
    const OptionsWindow window = OpenWindow(
        OptionsSettings{.autoAttack = false, .noticeSound = true, .slidesOn = true, .volume = 7, .glowDetail = 2});

    EXPECT_FALSE(window.Check(0).Checked());
    EXPECT_TRUE(window.Check(1).Checked());
    EXPECT_TRUE(window.Check(2).Checked());
    EXPECT_EQ(window.SliderAt(0).Value(), 7);
    EXPECT_EQ(window.SliderAt(0).Max(), 9);
    EXPECT_EQ(window.SliderAt(1).Value(), 2);
    EXPECT_EQ(window.SliderAt(1).Max(), 4);
    EXPECT_TRUE(window.SliderAt(0).IsOpen());
}

// 0x644730
TEST(OptionsWindowTest, EachCheckWritesItsGlobal)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;

    const std::optional<OptionsEvent> attack = ClickOn(window, input, window.Check(0).Area(), current);
    EXPECT_EQ(attack, (OptionsEvent{.kind = OptionsEvent::Kind::AutoAttack, .value = 0}));
    Client::FrontEnd::ApplyOption(current, *attack);
    EXPECT_FALSE(current.autoAttack);

    EXPECT_EQ(ClickOn(window, input, window.Check(1).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::NoticeSound, .value = 1}));
    EXPECT_EQ(ClickOn(window, input, window.Check(2).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::SlidesOn, .value = 0}));
    EXPECT_TRUE(window.IsOpen());
}

// The checkbox ends the step: an Esc of the same step is left for the scene (Q-C502).
TEST(OptionsWindowTest, ACheckEndsTheStepBeforeEscape)
{
    const OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(ClickOn(window, input, window.Check(0).Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::AutoAttack, .value = 0}));
    EXPECT_TRUE(window.IsOpen());
}

// The button closes without sound; Esc, with 0x19 (from CL10), and takes the press.
TEST(OptionsWindowTest, TheButtonAndEscapeClose)
{
    const OptionsSettings current;
    OptionsWindow button = OpenWindow(current);
    InputState input;
    EXPECT_EQ(ClickOn(button, input, button.CloseButton().Area(), current),
              (OptionsEvent{.kind = OptionsEvent::Kind::Closed}));
    EXPECT_FALSE(button.IsOpen());

    OptionsWindow escape = OpenWindow(current);
    Client::Tests::PressKey(input, Key::Escape);
    EXPECT_EQ(escape.Step(input, Screen640, StepMs, current), (OptionsEvent{.kind = OptionsEvent::Kind::Closed}));
    EXPECT_FALSE(escape.IsOpen());
    EXPECT_FALSE(input.TakePress(Key::Escape));
}

// Without the focus, 0x644730 does not run: neither Esc nor the sliders.
TEST(OptionsWindowTest, WithoutTheFocusNothingHappens)
{
    const OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs, current, NewUiTurn{.mouse = false, .logic = false}), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_TRUE(input.TakePress(Key::Escape));
}

// 0x6447d9: the slider that moves writes its value if it differs from the global, and ends the step anyway if it is
// the same. Pressing right of the volume knob (5, at 312) raises it to 6.
TEST(OptionsWindowTest, TheVolumeSliderWritesANewValue)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::MoveMouse(input, 350, 268);
    Client::Tests::PressLeft(input);

    const std::optional<OptionsEvent> volume = StepOnce(window, input, current);
    EXPECT_EQ(volume, (OptionsEvent{.kind = OptionsEvent::Kind::Volume, .value = 6}));
    Client::FrontEnd::ApplyOption(current, *volume);
    EXPECT_EQ(current.volume, 6);

    // Still pressed: the same value writes nothing, and Esc is not read.
    Client::Tests::PressKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input, current), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// 0x64480a: the glow, with the knob at 4 (at the end, 350): pressing left of it lowers it to 3.
TEST(OptionsWindowTest, TheGlowSliderWritesItsByte)
{
    OptionsSettings current;
    OptionsWindow window = OpenWindow(current);
    InputState input;
    Client::Tests::MoveMouse(input, 300, 330);
    Client::Tests::PressLeft(input);

    const std::optional<OptionsEvent> glow = StepOnce(window, input, current);
    EXPECT_EQ(glow, (OptionsEvent{.kind = OptionsEvent::Kind::GlowDetail, .value = 3}));
    Client::FrontEnd::ApplyOption(current, *glow);
    EXPECT_EQ(current.glowDetail, 3);
}

// 0x644860: the title centered in the frame 10 from the top, the checkbox labels 24 to the right and 4 down, the
// slider labels 18 up with the globals' value 85 to the right (the glow, 2 · d + 5) and, at the end, the button's
// label.
TEST(OptionsWindowTest, TheTextsShowTheGlobals)
{
    const auto texts = Client::Tests::MakeTextTable({{385, "Titulo"},
                                                     {386, "Uno"},
                                                     {387, "Dos"},
                                                     {919, "Tres"},
                                                     {389, "Volumen"},
                                                     {1840, "Brillo"},
                                                     {388, "Cerrar"}});
    const OptionsSettings armed;
    OptionsWindow window(LabSizes(), &texts);
    window.Arm(armed);
    window.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640, OptionsSettings{.volume = 7, .glowDetail = 2});

    ASSERT_EQ(text.texts.size(), 9U);
    EXPECT_EQ(text.texts[0].text, "Titulo");
    EXPECT_EQ(text.texts[0].x, 301);
    EXPECT_EQ(text.texts[0].y, 76);
    EXPECT_EQ(text.texts[1].text, "Uno");
    EXPECT_EQ(text.texts[1].x, 289);
    EXPECT_EQ(text.texts[1].y, 122);
    EXPECT_EQ(text.texts[3].text, "Tres");
    EXPECT_EQ(text.texts[3].y, 204);
    EXPECT_EQ(text.texts[4].text, "Volumen");
    EXPECT_EQ(text.texts[4].x, 265);
    EXPECT_EQ(text.texts[4].y, 246);
    EXPECT_EQ(text.texts[5].text, "7");
    EXPECT_EQ(text.texts[5].x, 350);
    EXPECT_EQ(text.texts[6].text, "Brillo");
    EXPECT_EQ(text.texts[6].y, 307);
    EXPECT_EQ(text.texts[7].text, "9");
    EXPECT_EQ(text.texts[8].text, "Cerrar");
}
} // namespace
