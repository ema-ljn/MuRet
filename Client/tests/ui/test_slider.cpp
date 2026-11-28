#include "Ui/Slider.h"

#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Ui/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

// The options' horizontal slider (vtable 0x6ba9f8: 0x646640, 0x646830, 0x646900, 0x646990, 0x646a50 and 0x646e90;
// CL8, milestone 8). All in pixels.
namespace
{
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Tests::MoveMouse;
using Client::Tests::PressLeft;
using Client::Tests::ReleaseLeft;
using Client::Ui::Point;
using Client::Ui::Rect;
using Client::Ui::Slider;
using Client::Ui::SliderParts;

// The options' parts: the 13 × 13 thumb, the 98 × 13 track and the fill at (3, 3, 95, 10): the thumb travels 85.
constexpr SliderParts Parts{.thumb = {.slot = TextureSlot{0x732}, .width = 13, .height = 13},
                            .track = {.slot = TextureSlot{0x734}, .width = 98, .height = 13},
                            .fill = {.slot = TextureSlot{0x733}, .width = 4, .height = 7},
                            .fillArea = Rect{.x = 3, .y = 3, .width = 92, .height = 7}};
constexpr Point Corner{100, 200};
// Kernel::LogicStep
constexpr double StepMs = 40.0;

Slider OpenSlider(int max, int value)
{
    Slider slider(Parts);
    slider.SetMax(max);
    slider.SetValue(value);
    slider.MoveTo(Corner);
    slider.Open(true);
    return slider;
}

int ThumbX(const Slider& slider)
{
    return slider.Thumb().Area().x;
}

// After the step the edges are cleared, as in GameLoop.
void StepOnce(Slider& slider, InputState& input, Point mouse)
{
    MoveMouse(input, mouse.x, mouse.y);
    slider.Step(input, mouse, StepMs);
    input.ConsumeEdges();
}

// 0x646830: x + 85 / max · value, truncated.
TEST(SliderTest, TheThumbGoesWhereItsValueIs)
{
    constexpr int Expected[] = {100, 109, 118, 128, 137, 147, 156, 166, 175, 185};
    for (int value = 0; value <= 9; ++value)
    {
        EXPECT_EQ(ThumbX(OpenSlider(9, value)), Expected[value]) << "valor " << value;
    }
    EXPECT_EQ(ThumbX(OpenSlider(4, 1)), 121);
    EXPECT_EQ(ThumbX(OpenSlider(4, 3)), 163);
}

// 0x6468e0 and 0x646f80: the max is 1 or more, and the value stays between 0 and the max.
TEST(SliderTest, TheValueStaysInsideTheRange)
{
    Slider slider(Parts);
    slider.SetMax(0);
    EXPECT_EQ(slider.Max(), 1);
    slider.SetMax(9);
    slider.SetValue(12);
    EXPECT_EQ(slider.Value(), 9);
    slider.SetValue(-3);
    EXPECT_EQ(slider.Value(), 0);
}

// 0x646a50: pressing beside the thumb moves it one step toward that side; with the button held, after 500 ms it
// repeats every 50. At 40 ms: the 13th step adds 520 and the repeater 40, and the 14th moves; then one in two. At the
// max, 0x646990 puts it at the end of the travel.
TEST(SliderTest, HoldingBesideTheThumbRepeats)
{
    Slider slider = OpenSlider(9, 5);
    InputState input;
    MoveMouse(input, 195, 205);
    PressLeft(input);
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 6);
    EXPECT_EQ(ThumbX(slider), 156);
    EXPECT_TRUE(slider.Moving());

    for (int step = 1; step <= 13; ++step)
    {
        StepOnce(slider, input, Point{195, 205});
        ASSERT_EQ(slider.Value(), 6) << "paso " << step;
    }
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 7);
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 7);
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 8);
    StepOnce(slider, input, Point{195, 205});
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 9);
    EXPECT_EQ(ThumbX(slider), 185);

    // On release, the thumb goes back to its value's place and stops moving.
    ReleaseLeft(input);
    StepOnce(slider, input, Point{195, 205});
    EXPECT_FALSE(slider.Moving());
    EXPECT_EQ(slider.Value(), 9);
}

TEST(SliderTest, PressingLeftOfTheThumbStepsBack)
{
    Slider slider = OpenSlider(4, 2);
    InputState input;
    MoveMouse(input, 105, 205);
    PressLeft(input);
    StepOnce(slider, input, Point{105, 205});

    EXPECT_EQ(slider.Value(), 1);
    EXPECT_EQ(ThumbX(slider), 121);
    EXPECT_TRUE(slider.Moving());
}

// A press to the right of the thumb moves it one step and, if it lands under the mouse, grabs it in the same press
// (Q-C503): from there it drags it. At 165, the thumb of 5 (147) goes to 156 and lands under it; with the mouse at
// 180, it goes to 171, the 8.
TEST(SliderTest, AStepThatLandsUnderTheMouseGrabsTheThumb)
{
    Slider slider = OpenSlider(9, 5);
    InputState input;
    MoveMouse(input, 165, 205);
    PressLeft(input);
    StepOnce(slider, input, Point{165, 205});
    EXPECT_EQ(slider.Value(), 6);
    EXPECT_EQ(ThumbX(slider), 156);

    StepOnce(slider, input, Point{180, 205});
    EXPECT_EQ(ThumbX(slider), 171);
    EXPECT_EQ(slider.Value(), 8);
}

// Grabbed at x = 105, the thumb follows the mouse up to the end of the travel, and the value is the nearest:
// (45 + 4.72) / 9.44 gives 5 and (85 + 4.72) / 9.44, 9.
TEST(SliderTest, DraggingTheThumbRoundsToTheNearestValue)
{
    Slider slider = OpenSlider(9, 0);
    InputState input;
    MoveMouse(input, 105, 205);
    PressLeft(input);
    StepOnce(slider, input, Point{105, 205});
    EXPECT_EQ(slider.Value(), 0);
    EXPECT_TRUE(slider.Moving());

    StepOnce(slider, input, Point{150, 205});
    EXPECT_EQ(ThumbX(slider), 145);
    EXPECT_EQ(slider.Value(), 5);

    StepOnce(slider, input, Point{300, 205});
    EXPECT_EQ(ThumbX(slider), 185);
    EXPECT_EQ(slider.Value(), 9);

    // Left of where it was grabbed: at the start.
    StepOnce(slider, input, Point{50, 205});
    EXPECT_EQ(ThumbX(slider), 100);
    EXPECT_EQ(slider.Value(), 0);
}

// A press and a release in the same step: the slider moves in that step and the release waits for the next, like the
// original's two frames (Q-C498).
TEST(SliderTest, AClickInOneStepReleasesInTheNext)
{
    Slider slider = OpenSlider(9, 5);
    InputState input;
    Client::Tests::ClickAt(input, 195, 205);
    StepOnce(slider, input, Point{195, 205});
    EXPECT_EQ(slider.Value(), 6);
    EXPECT_TRUE(slider.Moving());

    StepOnce(slider, input, Point{195, 205});
    EXPECT_FALSE(slider.Moving());
    EXPECT_EQ(slider.Value(), 6);
}

// Closed, 0x646a50 does not run.
TEST(SliderTest, AClosedSliderDoesNotMove)
{
    Slider slider = OpenSlider(9, 5);
    slider.Open(false);
    InputState input;
    MoveMouse(input, 195, 205);
    PressLeft(input);
    StepOnce(slider, input, Point{195, 205});

    EXPECT_EQ(slider.Value(), 5);
    EXPECT_FALSE(slider.Moving());
}

// 0x640580: the fill measures 92 · value / max, in integers; 0x646e90 draws the track, the fill at (3, 3) with its
// width and height (the UVs without half a texel) and the thumb.
TEST(SliderTest, TheFillFollowsTheValue)
{
    EXPECT_EQ(OpenSlider(9, 0).FillWidth(), 0);
    EXPECT_EQ(OpenSlider(9, 5).FillWidth(), 51);
    EXPECT_EQ(OpenSlider(9, 9).FillWidth(), 92);
    EXPECT_EQ(OpenSlider(4, 3).FillWidth(), 69);

    const auto images = OpenSlider(9, 5).Images(Client::Tests::FakeTextureSizes({}));
    EXPECT_EQ(images[0].slot, TextureSlot{0x734});
    EXPECT_EQ(images[0].x, 100);
    EXPECT_EQ(images[0].width, 98);
    EXPECT_EQ(images[1].slot, TextureSlot{0x733});
    EXPECT_EQ(images[1].x, 103);
    EXPECT_EQ(images[1].y, 203);
    EXPECT_EQ(images[1].width, 51);
    EXPECT_EQ(images[1].height, 7);
    EXPECT_TRUE(images[1].resizedWidth);
    EXPECT_TRUE(images[1].resizedHeight);
    EXPECT_EQ(images[2].slot, TextureSlot{0x732});
    EXPECT_EQ(images[2].x, 147);
}
} // namespace
