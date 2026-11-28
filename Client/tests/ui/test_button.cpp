#include "Ui/Button.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Ui/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

// Client::Ui::Button: the new UI button (0x63c330, 0x63c490 and 0x63c710), in the step (CL5.5). It captures on a
// press over it and clicks on a release over it; its frames, one below the other: 0 normal, 1 hovered, 2 pressed, and
// the checked one (3 in the group ones).
namespace
{
using Client::Core::PixelSize;
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Tests::ClickAt;
using Client::Tests::MoveMouse;
using Client::Tests::PressLeft;
using Client::Tests::ReleaseLeft;
using Client::Ui::Button;
using Client::Ui::ButtonColors;
using Client::Ui::MouseInPixels;
using Client::Ui::Point;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

// A group button: 108 × 26, cha_bt (slot 0xc) with four frames, 3 checked.
Button GroupButton()
{
    Button button(TextureSlot{0xc}, 108, 26, 3);
    button.MoveTo(Point{100, 50});
    return button;
}

// A button step with the mouse of `input`; then the edges are cleared as in GameLoop.
bool StepOnce(Button& button, InputState& input)
{
    const bool clicked = button.Step(input, MouseInPixels(input, Screen640));
    input.ConsumeEdges();
    return clicked;
}

TEST(ButtonTest, AClickIsAPressAndAReleaseOverIt)
{
    Button button = GroupButton();
    InputState input;
    MoveMouse(input, 120, 60);
    PressLeft(input);
    EXPECT_FALSE(StepOnce(button, input));

    ReleaseLeft(input);
    EXPECT_TRUE(StepOnce(button, input));
    EXPECT_FALSE(StepOnce(button, input));
}

// The step sees both edges.
TEST(ButtonTest, APressAndAReleaseInOneStepAreAClick)
{
    Button button = GroupButton();
    InputState input;
    ClickAt(input, 120, 60);

    EXPECT_TRUE(StepOnce(button, input));
}

TEST(ButtonTest, TheClickCallbackRunsOnlyForACapturedReleaseBeforeTheCheckedToggle)
{
    Button button = GroupButton();
    InputState input;
    int clicks = 0;
    const auto onClick = [&]
    {
        ++clicks;
        EXPECT_FALSE(button.Checked());
    };

    MoveMouse(input, 120, 60);
    PressLeft(input);
    EXPECT_FALSE(button.Step(input, MouseInPixels(input, Screen640), onClick));
    input.ConsumeEdges();
    ReleaseLeft(input);
    EXPECT_TRUE(button.Step(input, MouseInPixels(input, Screen640), onClick));
    EXPECT_TRUE(button.Checked());
    EXPECT_EQ(clicks, 1);
    input.ConsumeEdges();

    MoveMouse(input, 10, 10);
    PressLeft(input);
    EXPECT_FALSE(button.Step(input, MouseInPixels(input, Screen640), onClick));
    input.ConsumeEdges();
    MoveMouse(input, 120, 60);
    ReleaseLeft(input);
    EXPECT_FALSE(button.Step(input, MouseInPixels(input, Screen640), onClick));
    EXPECT_EQ(clicks, 1);
    button.SetEnabled(false);
    ClickAt(input, 120, 60);
    EXPECT_FALSE(button.Step(input, MouseInPixels(input, Screen640), onClick));
    EXPECT_EQ(clicks, 1);
}

// 0x63c490 clicks only if the button captured the press: a release over it after a press elsewhere does not count,
// and a release outside lets go without a click.
TEST(ButtonTest, OnlyTheButtonThatCapturedIsClicked)
{
    Button button = GroupButton();
    InputState input;
    MoveMouse(input, 10, 10);
    PressLeft(input);
    EXPECT_FALSE(StepOnce(button, input));
    MoveMouse(input, 120, 60);
    ReleaseLeft(input);
    EXPECT_FALSE(StepOnce(button, input));

    PressLeft(input);
    EXPECT_FALSE(StepOnce(button, input));
    MoveMouse(input, 10, 10);
    ReleaseLeft(input);
    EXPECT_FALSE(StepOnce(button, input));
    MoveMouse(input, 120, 60);
    EXPECT_FALSE(StepOnce(button, input));
}

TEST(ButtonTest, TheFrameFollowsTheMouse)
{
    Button button = GroupButton();
    InputState input;
    MoveMouse(input, 10, 10);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 0);

    MoveMouse(input, 120, 60);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 1);

    PressLeft(input);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 2);
    EXPECT_TRUE(button.Pressed());

    // Pressed and outside: the normal frame, though still captured.
    MoveMouse(input, 10, 10);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 0);
}

// The group ones ask for 3 for checked, pressed, hovered or not (0x6451d0 passes -1 for the other two, which 0x63c330
// turns into 3).
TEST(ButtonTest, ACheckedButtonShowsItsCheckedFrame)
{
    Button button = GroupButton();
    button.SetChecked(true);
    InputState input;
    MoveMouse(input, 10, 10);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 3);

    MoveMouse(input, 120, 60);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 3);

    PressLeft(input);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 3);
}

TEST(ButtonTest, AButtonWithoutACheckedFrameIgnoresTheCheck)
{
    Button button(TextureSlot{0xd}, 193, 26);
    button.MoveTo(Point{0, 0});
    button.SetChecked(true);
    InputState input;
    MoveMouse(input, 300, 300);
    StepOnce(button, input);

    EXPECT_EQ(button.Frame(), 0);
}

// Frame k starts at k · h texels.
TEST(ButtonTest, TheImageShowsTheFrame)
{
    Button button = GroupButton();
    InputState input;
    MoveMouse(input, 120, 60);
    StepOnce(button, input);

    const auto sizes = Client::Tests::FakeTextureSizes({{0xc, {128, 128}}});
    const auto image = button.Image(sizes);
    ASSERT_TRUE(image.slot.has_value());
    EXPECT_EQ(*image.slot, TextureSlot{0xc});
    EXPECT_EQ(image.x, 100);
    EXPECT_EQ(image.y, 50);
    EXPECT_EQ(image.width, 108);
    EXPECT_EQ(image.height, 26);
    EXPECT_EQ(image.textureWidth, 128.0f);
    EXPECT_EQ(image.textureHeight, 128.0f);
    EXPECT_EQ(image.frameTop, std::optional<int>{26});
    EXPECT_EQ(button.Area(), (Rect{.x = 100, .y = 50, .width = 108, .height = 26}));
}

// +0xa8 at 0, 0x63c490: no click, never hovered or pressed; it shows its disabled frame, 3 in the selection bar's
// ones.
TEST(ButtonTest, ADisabledButtonHasNoClickAndShowsItsFrame)
{
    Button button(TextureSlot{0xf}, 54, 30, std::nullopt, 3);
    button.MoveTo(Point{100, 50});
    button.SetEnabled(false);
    InputState input;
    MoveMouse(input, 120, 60);
    PressLeft(input);
    EXPECT_FALSE(StepOnce(button, input));
    EXPECT_EQ(button.Frame(), 3);
    ReleaseLeft(input);
    EXPECT_FALSE(StepOnce(button, input));
    EXPECT_FALSE(button.Enabled());

    // Enabled again, the click comes back; one without a disabled frame shows the normal one.
    button.SetEnabled(true);
    PressLeft(input);
    StepOnce(button, input);
    ReleaseLeft(input);
    EXPECT_TRUE(StepOnce(button, input));
    Button plain = GroupButton();
    plain.SetEnabled(false);
    EXPECT_EQ(plain.Frame(), 0);
}

// Disabled, the label takes its color ([3], or [7] checked).
TEST(ButtonTest, ADisabledLabelHasItsColor)
{
    Button button = GroupButton();
    button.SetLabel("abc", ButtonColors{.disabled = 0xff777777, .checkedDisabled = 0xff666666});
    button.SetEnabled(false);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    EXPECT_EQ(button.DrawLabel(text, Screen640), std::optional<std::uint32_t>{0xff777777});
    button.SetChecked(true);
    EXPECT_EQ(button.DrawLabel(text, Screen640), std::optional<std::uint32_t>{0xff666666});
}

// 0x63c710: the label goes with font DAT_05763560, the state's color and background 0, centered in the button's width
// (0x467060's mode 2), and the y in 640 × 480 is ((h − cy) · 0.5 + y) / ry plus 0.5, or 1.5 pressed, truncated. In
// 640 × 480, a 3-letter label (18 × 13) on a 108 × 26 button at (100, 50) goes in a 108-wide box from (100, 57), and
// pressed from (100, 58), with the text at column (108 − 18) / 2 = 45.
TEST(ButtonTest, TheLabelIsCenteredWithTheColorOfItsState)
{
    Button button = GroupButton();
    button.SetLabel("abc", ButtonColors{.normal = 0xffe2e2e2, .pressed = 0xffe2e2e2, .hover = 0xffffffff});
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);
    InputState input;
    MoveMouse(input, 10, 10);
    StepOnce(button, input);
    button.DrawLabel(text, Screen640);

    MoveMouse(input, 120, 60);
    StepOnce(button, input);
    // Returns the color it drew with (Q-C83).
    EXPECT_EQ(button.DrawLabel(text, Screen640), std::optional<std::uint32_t>{0xffffffff});

    PressLeft(input);
    StepOnce(button, input);
    button.DrawLabel(text, Screen640);

    ASSERT_EQ(text.texts.size(), 3u);
    EXPECT_EQ(text.texts[0].text, "abc");
    EXPECT_EQ(text.texts[0].x, 100);
    EXPECT_EQ(text.texts[0].y, 57);
    EXPECT_TRUE(text.texts[0].windowed);
    EXPECT_EQ(text.texts[0].firstColumn, -45);
    EXPECT_EQ(text.texts[0].columns, 108);
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::OtherFace12);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffe2e2e2u);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(text.texts[1].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[2].y, 58);
    EXPECT_EQ(text.texts[2].style.colors.text, 0xffe2e2e2u);
}

// In 1024 × 768, the button's x goes to 640 × 480 with DesignX (100 / 1.6000001 gives 62) and the box starts there,
// with the text 45 columns in; the y, 56.5 / 1.6000001 + 0.5, ends at 35.
TEST(ButtonTest, TheLabelGoesToTheDesignSpace)
{
    Button button = GroupButton();
    button.SetLabel("abc", ButtonColors{});
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen1024);
    Client::Tests::RecordingTextDrawer text(batch);

    button.DrawLabel(text, Screen1024);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].x, 62);
    EXPECT_EQ(text.texts[0].y, 35);
    EXPECT_EQ(text.texts[0].firstColumn, -45);
    EXPECT_EQ(text.texts[0].columns, 108);
}

// Without a screen (a side at 0) the label has nowhere to go: the y is divided by the ratio.
TEST(ButtonTest, WithoutAScreenTheLabelIsNotDrawn)
{
    Button button = GroupButton();
    button.SetLabel("abc", ButtonColors{});
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    EXPECT_EQ(button.DrawLabel(text, PixelSize{0, 0}), std::nullopt);
    EXPECT_EQ(button.DrawLabel(text, PixelSize{640, 0}), std::nullopt);

    EXPECT_TRUE(text.texts.empty());
}

TEST(ButtonTest, WithoutALabelThereIsNoText)
{
    const Button button = GroupButton();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    EXPECT_EQ(button.DrawLabel(text, Screen640), std::nullopt);

    EXPECT_TRUE(text.texts.empty());
}
} // namespace
