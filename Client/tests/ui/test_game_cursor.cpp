#include "Ui/GameCursor.h"

#include "Core/PixelSize.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Ui/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

// Client::Ui: the game cursor (0x5ac2d0), with the WndProc mouse (0x4b358f).
namespace
{
using Client::Core::PixelSize;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::OriginalQuad;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::Ui::CursorQuad;
using Client::Ui::DrawGameCursor;
using Client::Ui::MouseIn640x480;
using Client::Ui::Point;

constexpr PixelSize Screen1024{1024, 768};

// LOWORD · 640 / W and HIWORD · 480 / H, in unsigned integers: the whole pixel, and the division truncates.
TEST(GameCursorTest, TheMouseOfTheWndProcIsIn640x480)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = 512.75f, .y = 384.5f});
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{320, 240}));

    Client::Tests::MoveMouse(input, 1023, 767);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{639, 479}));

    Client::Tests::MoveMouse(input, 3, 5);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{1, 3}));
}

// Unclamped (Q-C47): past the edge it keeps going, and a negative one is a large LOWORD. The original does not get the
// mouse outside the window (it does not capture it); MuRet does, with a button down (08, P-C27 g).
TEST(GameCursorTest, TheMouseOfTheWndProcIsNotClamped)
{
    InputState input;
    Client::Tests::MoveMouse(input, 2000, 1000);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{1250, 625}));

    Client::Tests::MoveMouse(input, -1, -2);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{65535 * 640 / 1024, 65534 * 480 / 768}));

    // −0.5 is pixel −1, not 0.
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = -0.5f, .y = 0.5f});
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{65535 * 640 / 1024, 0}));
}

// What is not a number places nothing, and an empty screen does not divide.
TEST(GameCursorTest, ANanMouseOrAnEmptyScreenIsZero)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion,
                           .x = std::numeric_limits<float>::quiet_NaN(),
                           .y = std::numeric_limits<float>::infinity()});
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{0, 0}));

    Client::Tests::MoveMouse(input, 100, 100);
    EXPECT_EQ(MouseIn640x480(input, PixelSize{0, 0}), (Point{0, 0}));
}

// 0x5ac7da: Interface\Cursor.tga (slot 2), 24 × 24 at (x − 2, y − 2), scaled in size and position, whole
// (0, 0, 1, 1) and without alpha.
TEST(GameCursorTest, TheCursorIsTheSlot2Around)
{
    const OriginalQuad quad = CursorQuad(Point{320, 240}, false);

    EXPECT_EQ(quad.slot, (TextureSlot{2}));
    EXPECT_EQ(quad.x, 318.0f);
    EXPECT_EQ(quad.y, 238.0f);
    EXPECT_EQ(quad.width, 24.0f);
    EXPECT_EQ(quad.height, 24.0f);
    EXPECT_EQ(quad.u, 0.0f);
    EXPECT_EQ(quad.v, 0.0f);
    EXPECT_EQ(quad.du, 1.0f);
    EXPECT_EQ(quad.dv, 1.0f);
    EXPECT_TRUE(quad.scaleSize);
    EXPECT_TRUE(quad.scalePosition);
    EXPECT_EQ(quad.alpha, 0.0f);
}

// 0x5ac76e: with the left button down (0x7d36874), Interface\CursorPush.tga (slot 3), at the same place.
TEST(GameCursorTest, WithTheLeftButtonDownItIsTheSlot3)
{
    const OriginalQuad quad = CursorQuad(Point{0, 1}, true);

    EXPECT_EQ(quad.slot, (TextureSlot{3}));
    EXPECT_EQ(quad.x, -2.0f);
    EXPECT_EQ(quad.y, -1.0f);
}

// 0x620a00(1) and glColor3f(1, 1, 1) before the quad: the normal blend with the alpha test and white, overriding what
// the UI left. In 1024 × 768 the cursor is 38.4 × 38.4 and its top corner is at (508.8, 768 − 380.8).
TEST(GameCursorTest, TheCursorIsDrawnWhiteWithTheNormalBlend)
{
    InputState input;
    Client::Tests::MoveMouse(input, 512, 384);
    Client::Tests::PressLeft(input);
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    sprites.SetBlend(BlendMode::Additive);
    sprites.SetColor(Color{0.8f, 0.7f, 0.6f, 1.0f});

    DrawGameCursor(sprites, input, Screen1024);

    ASSERT_EQ(sprites.Commands().size(), 1u);
    const auto& command = sprites.Commands().back();
    EXPECT_EQ(command.texture, (TextureSlot{3}));
    EXPECT_EQ(command.blend, BlendMode::Alpha);
    EXPECT_TRUE(command.alphaTest);
    ASSERT_EQ(command.vertexCount, 6u);
    float left = 1e9f;
    float right = -1e9f;
    float bottom = 1e9f;
    float top = -1e9f;
    for (const auto& vertex : sprites.Vertices())
    {
        EXPECT_EQ(vertex.color, Color{});
        left = std::min(left, vertex.x);
        right = std::max(right, vertex.x);
        bottom = std::min(bottom, vertex.y);
        top = std::max(top, vertex.y);
    }
    EXPECT_FLOAT_EQ(left, 508.8f);
    EXPECT_FLOAT_EQ(right - left, 38.4f);
    EXPECT_FLOAT_EQ(top, 768.0f - 380.8f);
    EXPECT_FLOAT_EQ(top - bottom, 38.4f);
}
} // namespace
