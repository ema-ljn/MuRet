#include "Ui/Widget.h"

#include "Core/PixelSize.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Assets/SceneTextures.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <limits>

namespace
{
using Client::Assets::TextureBufferSize;
using Client::Core::PixelSize;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Ui::MouseInPixels;
using Client::Ui::PixelImage;
using Client::Ui::Point;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};

// The mouse arrives in framebuffer pixels, with a fraction; the UI uses the pixel, like the original's WM_MOUSEMOVE
// lParam.
TEST(WidgetTest, TheMouseIsTheWholePixel)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = 12.75f, .y = 40.25f});

    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{12, 40}));
}

// 0x640940: the new UI's position stays inside the window, 0 to W − 1 and 0 to H − 1. With the mouse captured, SDL
// gives negatives or values past the edge.
TEST(WidgetTest, TheMouseIsClampedToTheWindow)
{
    InputState input;
    Client::Tests::MoveMouse(input, -5, 700);
    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{0, 479}));

    Client::Tests::MoveMouse(input, 900, -1);
    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{639, 0}));
}

TEST(WidgetTest, ANanMouseIsZero)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion,
                           .x = std::numeric_limits<float>::quiet_NaN(),
                           .y = std::numeric_limits<float>::infinity()});

    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{0, 479}));
}

// Like PtInRect: the left and top edges belong to the rectangle; the right and bottom ones do not.
TEST(WidgetTest, ARectHasItsLeftAndTopEdges)
{
    const Rect rect{.x = 10, .y = 20, .width = 5, .height = 3};

    EXPECT_TRUE(rect.Contains(Point{10, 20}));
    EXPECT_TRUE(rect.Contains(Point{14, 22}));
    EXPECT_FALSE(rect.Contains(Point{15, 22}));
    EXPECT_FALSE(rect.Contains(Point{14, 23}));
    EXPECT_FALSE(rect.Contains(Point{9, 20}));
    EXPECT_FALSE(rect.Contains(Point{10, 19}));
}

TEST(WidgetTest, APixelImageHasItsSlotAndItsBuffer)
{
    const auto image = PixelImage(TextureSlot{0x13}, Rect{.x = 155, .y = 156, .width = 329, .height = 245},
                                  TextureBufferSize{512, 256});

    ASSERT_TRUE(image.slot.has_value());
    EXPECT_EQ(*image.slot, TextureSlot{0x13});
    EXPECT_EQ(image.x, 155);
    EXPECT_EQ(image.y, 156);
    EXPECT_EQ(image.width, 329);
    EXPECT_EQ(image.height, 245);
    EXPECT_EQ(image.textureWidth, 512.0f);
    EXPECT_EQ(image.textureHeight, 256.0f);
    EXPECT_EQ(image.scaleX, 1.0f);
    EXPECT_EQ(image.scaleY, 1.0f);
    EXPECT_FALSE(image.frameTop.has_value());
}
} // namespace
