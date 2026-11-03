#include "UI/Widget.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneTextures.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <limits>

// Client::UI: el mouse de la UI y los rectángulos (CL5.5, la pregunta 12 del plan de la CL5).
namespace
{
using Client::App::PixelSize;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Scenes::TextureBufferSize;
using Client::UI::MouseInPixels;
using Client::UI::PixelImage;
using Client::UI::Point;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};

// El mouse llega en píxeles del framebuffer, con fracción; la UI usa el píxel, como el lParam del
// WM_MOUSEMOVE del original.
TEST(WidgetTest, TheMouseIsTheWholePixel)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion, .x = 12.75f, .y = 40.25f});

    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{12, 40}));
}

// 0x640940: la posición de la UI nueva queda adentro de la ventana, de 0 a W − 1 y de 0 a H − 1.
// Con el mouse capturado SDL da negativos o más allá del borde.
TEST(WidgetTest, TheMouseIsClampedToTheWindow)
{
    InputState input;
    Client::Tests::MoveMouse(input, -5, 700);
    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{0, 479}));

    Client::Tests::MoveMouse(input, 900, -1);
    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{639, 0}));
}

// Una posición que no es un número no puede ubicar nada: queda en 0.
TEST(WidgetTest, ANanMouseIsZero)
{
    InputState input;
    input.Apply(InputEvent{.kind = InputEvent::Kind::Motion,
                           .x = std::numeric_limits<float>::quiet_NaN(),
                           .y = std::numeric_limits<float>::infinity()});

    EXPECT_EQ(MouseInPixels(input, Screen640), (Point{0, 479}));
}

// Como PtInRect: el borde de la izquierda y el de arriba son del rectángulo; el de la derecha y el
// de abajo, no.
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

// La imagen de una ventana va en píxeles (fx = fy = 1), con el buffer de su textura.
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
