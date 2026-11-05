#include "UI/GameCursor.h"

#include "App/WindowState.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "UI/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

// Client::UI: el cursor del juego (0x5ac2d0), con el mouse del WndProc (0x4b358f).
namespace
{
using Client::App::PixelSize;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::OriginalQuad;
using Client::Render::SpriteBatch;
using Client::Render::TextureSlot;
using Client::UI::CursorQuad;
using Client::UI::DrawGameCursor;
using Client::UI::MouseIn640x480;
using Client::UI::Point;

constexpr PixelSize Screen1024{1024, 768};

// LOWORD · 640 / W y HIWORD · 480 / H, en enteros sin signo: el píxel entero, y la división trunca.
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

// Sin acotar (Q-C47): más allá del borde sigue de largo, y un negativo es un LOWORD grande. El original no
// recibe el mouse fuera de la ventana (no lo captura); MuRet sí, con un botón abajo (08, P-C27 g).
TEST(GameCursorTest, TheMouseOfTheWndProcIsNotClamped)
{
    InputState input;
    Client::Tests::MoveMouse(input, 2000, 1000);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{1250, 625}));

    Client::Tests::MoveMouse(input, -1, -2);
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{65535 * 640 / 1024, 65534 * 480 / 768}));

    // −0,5 es el píxel −1, no el 0.
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = -0.5f, .y = 0.5f});
    EXPECT_EQ(MouseIn640x480(input, Screen1024), (Point{65535 * 640 / 1024, 0}));
}

// Lo que no es un número no ubica nada, y una pantalla vacía no divide.
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

// 0x5ac7da: Interface\Cursor.tga (el slot 2), de 24 × 24 en (x − 2, y − 2), escalado en tamaño y en
// posición, entero (0, 0, 1, 1) y sin alfa.
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

// 0x5ac76e: con el botón izquierdo abajo (0x7d36874), Interface\CursorPush.tga (el slot 3), en el mismo
// lugar.
TEST(GameCursorTest, WithTheLeftButtonDownItIsTheSlot3)
{
    const OriginalQuad quad = CursorQuad(Point{0, 1}, true);

    EXPECT_EQ(quad.slot, (TextureSlot{3}));
    EXPECT_EQ(quad.x, -2.0f);
    EXPECT_EQ(quad.y, -1.0f);
}

// 0x620a00(1) y glColor3f(1, 1, 1) antes del quad: la mezcla normal con el alpha test y el blanco, pisando
// lo que dejó la UI. En 1024 × 768 el cursor mide 38,4 × 38,4 y su esquina de arriba está en
// (508,8; 768 − 380,8).
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
