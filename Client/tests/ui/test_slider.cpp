#include "UI/Slider.h"

#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "UI/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

// Client::UI::Slider: el deslizador horizontal de las opciones (vtable 0x6ba9f8: 0x646640, 0x646830,
// 0x646900, 0x646990, 0x646a50 y 0x646e90; CL8, hito 8). Todo en píxeles.
namespace
{
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Tests::MoveMouse;
using Client::Tests::PressLeft;
using Client::Tests::ReleaseLeft;
using Client::UI::Point;
using Client::UI::Rect;
using Client::UI::Slider;
using Client::UI::SliderParts;

// Las piezas de las opciones: la perilla de 13 × 13, la barra de 98 × 13 y el relleno en (3, 3, 95, 10):
// la perilla recorre 85.
constexpr SliderParts Parts{.thumb = {.slot = TextureSlot{0x732}, .width = 13, .height = 13},
                            .track = {.slot = TextureSlot{0x734}, .width = 98, .height = 13},
                            .fill = {.slot = TextureSlot{0x733}, .width = 4, .height = 7},
                            .fillArea = Rect{.x = 3, .y = 3, .width = 92, .height = 7}};
constexpr Point Corner{100, 200};
// Un paso de la lógica: 40 ms (App::LogicStep).
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

// Un paso con el mouse donde lo dejó `input`; después, los flancos se borran como en GameLoop.
void StepOnce(Slider& slider, InputState& input, Point mouse)
{
    MoveMouse(input, mouse.x, mouse.y);
    slider.Step(input, mouse, StepMs);
    input.ConsumeEdges();
}

// 0x646830: x + 85 / máximo · valor, truncado.
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

// 0x6468e0 y 0x646f80: el máximo es 1 o más, y el valor queda entre 0 y el máximo.
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

// 0x646a50: apretar al lado de la perilla la mueve un paso hacia ese lado; con el botón apretado, a los 500 ms
// repite cada 50. A 40 ms: el 13.º paso suma 520 y el repetidor 40, y el 14.º mueve; después, uno de cada dos.
// En el máximo, 0x646990 la pone al final del recorrido.
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

    // Al soltar, la perilla vuelve al lugar de su valor y deja de moverse.
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

// Un apretón a la derecha de la perilla la mueve un paso y, si quedó debajo del mouse, la agarra en el mismo
// apretón (Q-C503): desde ahí la arrastra. En 165, la perilla de 5 (147) pasa a 156 y queda debajo; con el mouse
// en 180, va a 171, el 8.
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

// Agarrada en x = 105, la perilla sigue al mouse hasta el final del recorrido, y el valor es el más cerca:
// (45 + 4,72) / 9,44 da 5 y (85 + 4,72) / 9,44, 9.
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

    // A la izquierda de donde se agarró, al principio.
    StepOnce(slider, input, Point{50, 205});
    EXPECT_EQ(ThumbX(slider), 100);
    EXPECT_EQ(slider.Value(), 0);
}

// Un apretón y una soltada en el mismo paso: el deslizador se mueve en ese paso y la soltada queda para el
// siguiente, como los dos frames del original (Q-C498).
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

// Cerrado, 0x646a50 no corre.
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

// 0x640580: el relleno mide 92 · valor / máximo, en enteros; 0x646e90 dibuja la barra, el relleno a (3, 3)
// con su ancho y su alto (las UV sin medio téxel) y la perilla.
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
