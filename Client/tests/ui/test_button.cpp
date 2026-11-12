#include "UI/Button.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "UI/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

// Client::UI::Button: el botón de la UI nueva (0x63c330, 0x63c490 y 0x63c710), en el paso (CL5.5).
// Captura con el apretón encima y hace clic con la soltada encima; sus cuadros, uno abajo del
// otro: 0 normal, 1 con el mouse encima, 2 apretado, y el marcado (3 en los de los grupos).
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Render::TextureSlot;
using Client::Tests::ClickAt;
using Client::Tests::MoveMouse;
using Client::Tests::PressLeft;
using Client::Tests::ReleaseLeft;
using Client::UI::Button;
using Client::UI::ButtonColors;
using Client::UI::MouseInPixels;
using Client::UI::Point;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

// Un botón de grupo: 108 × 26, cha_bt (slot 0xc) con cuatro cuadros, el 3 marcado.
Button GroupButton()
{
    Button button(TextureSlot{0xc}, 108, 26, 3);
    button.MoveTo(Point{100, 50});
    return button;
}

// Un paso del botón con el mouse de `input`; después, los flancos se borran como en GameLoop.
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

// Apretar y soltar entre dos pasos también es un clic: el paso ve los dos flancos.
TEST(ButtonTest, APressAndAReleaseInOneStepAreAClick)
{
    Button button = GroupButton();
    InputState input;
    ClickAt(input, 120, 60);

    EXPECT_TRUE(StepOnce(button, input));
}

// 0x63c490 hace clic solo si el botón capturó el apretón: soltar encima de otro lado no cuenta, y
// soltar afuera lo suelta sin clic.
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

    // Apretado y afuera: el normal, aunque siga capturado.
    MoveMouse(input, 10, 10);
    StepOnce(button, input);
    EXPECT_EQ(button.Frame(), 0);
}

// Los de los grupos piden el 3 para el marcado, apretado, encima o no (0x6451d0 le pasa -1 a los
// otros dos, que 0x63c330 cambia por el 3).
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

// Uno sin cuadro de marcado (el de un servidor, el OK) no cambia de cuadro marcado.
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

// La imagen, en píxeles, con el cuadro de ahora: el k empieza en k · h téxeles.
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

// Uno deshabilitado (+0xa8 en 0, 0x63c490) no tiene clic ni se ve encima ni apretado: muestra su
// cuadro de deshabilitado, el 3 en los de la barra de la selección.
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

    // Habilitado otra vez, el clic vuelve; uno sin cuadro de deshabilitado muestra el normal.
    button.SetEnabled(true);
    PressLeft(input);
    StepOnce(button, input);
    ReleaseLeft(input);
    EXPECT_TRUE(StepOnce(button, input));
    Button plain = GroupButton();
    plain.SetEnabled(false);
    EXPECT_EQ(plain.Frame(), 0);
}

// Deshabilitado, el rótulo va con su color ([3] o [7] marcado).
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

// 0x63c710: el rótulo va con la fuente DAT_05763560, el color del estado y fondo 0, centrado en
// el ancho del botón (el modo 2 de 0x467060), y la y en 640 × 480 es ((h − cy) · 0,5 + y) / ry más
// 0,5, o 1,5 apretado, truncada. En 640 × 480, un rótulo de 3 letras (18 × 13) en un botón de
// 108 × 26 en (100, 50) va en una caja de 108 desde (100, 57), y apretado desde (100, 58), con el
// texto en la columna (108 − 18) / 2 = 45.
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
    // Da el color con que lo dibujó (Q-C83).
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

// En 1024 × 768, la x del botón pasa a 640 × 480 con DesignX (100 / 1,6000001 da 62) y la caja empieza ahí, con el
// texto 45 columnas adentro; la y, 56,5 / 1,6000001 + 0,5, queda en 35.
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

// Sin pantalla (un lado en 0) el rótulo no tiene dónde ir: la y se divide por la proporción.
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

// Sin rótulo no se dibuja texto.
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
