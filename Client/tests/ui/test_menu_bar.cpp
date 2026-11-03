#include "UI/MenuBar.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "UI/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

// Client::UI::MenuBar: la barra de abajo del login (+0xe40: 0x641000, 0x65f300, 0x6410c0, 0x641440
// y 0x6411b0). Dos botones de 54 × 30 con tres cuadros, el menú a la izquierda y los créditos a la
// derecha, adentro del adorno de deco.tga. Todo en píxeles.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::UI::LayoutMenuBar;
using Client::UI::MenuBar;
using Client::UI::MenuBarClick;
using Client::UI::MenuBarLayout;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};

// Los buffers del laboratorio: server_menu_b_all y server_credit_b_all miden 54 × 90 (tres cuadros
// de 30), y deco.tga, 189 × 103.
Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes({{0x10, {64, 128}}, {0x11, {64, 128}}, {0x12, {256, 128}}});
}

int MiddleX(const Rect& rect)
{
    return rect.x + rect.width / 2;
}

int MiddleY(const Rect& rect)
{
    return rect.y + rect.height / 2;
}

std::optional<MenuBarClick> ClickOn(MenuBar& bar, const Rect& rect)
{
    InputState input;
    Client::Tests::ClickAt(input, MiddleX(rect), MiddleY(rect));
    return bar.Step(input, Screen640);
}

// La textura de cada quad del lote, en orden.
std::vector<std::uint16_t> QuadSlots(const Client::Render::SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const auto& command : batch.Commands())
    {
        const std::uint16_t slot = command.texture ? command.texture->value : std::uint16_t{0xffff};
        slots.insert(slots.end(), command.vertexCount / 6, slot);
    }
    return slots;
}

// 0x65f300: y = −11 − (int) (H · −0,945f) − 30. En 640 × 480, −11 + 453 − 30 = 412; el menú en la
// esquina, los créditos en 30 + 580 − 54 = 556 y el adorno con su punto (105, 59) en la esquina de
// ellos.
TEST(MenuBarTest, ItGoesAtTheBottomInPixels)
{
    const MenuBarLayout layout = LayoutMenuBar(Screen640);
    EXPECT_EQ(layout.bar, (Rect{.x = 30, .y = 412, .width = 580, .height = 30}));
    EXPECT_EQ(layout.menu, (Rect{.x = 30, .y = 412, .width = 54, .height = 30}));
    EXPECT_EQ(layout.credit, (Rect{.x = 556, .y = 412, .width = 54, .height = 30}));
    EXPECT_EQ(layout.ornament, (Rect{.x = 451, .y = 353, .width = 189, .height = 103}));

    const MenuBarLayout large = LayoutMenuBar(PixelSize{1024, 768});
    EXPECT_EQ(large.bar, (Rect{.x = 30, .y = 684, .width = 964, .height = 30}));
    EXPECT_EQ(large.credit, (Rect{.x = 940, .y = 684, .width = 54, .height = 30}));
    EXPECT_EQ(large.ornament, (Rect{.x = 835, .y = 625, .width = 189, .height = 103}));
}

// El float de 0x6baae0 es un poco más chico que 0,945 en valor absoluto, y el producto no se
// redondea a float: con 600, 600 · 0,94499999… = 566,99999… y _ftol da 566, así que la y es 525
// (526 con 0,945 exacto o con el producto en float). Lo mismo con 800: 714 y no 715.
TEST(MenuBarTest, TheHeightIsMultipliedInFullPrecision)
{
    EXPECT_EQ(LayoutMenuBar(PixelSize{800, 600}).bar.y, 525);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1280, 800}).bar.y, 714);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1280, 1024}).bar.y, 926);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1600, 1200}).bar.y, 1092);
}

// 0x641440: el adorno y después los dos botones, con la mezcla normal aunque el lote llegue con
// la aditiva (Q-C80). Sin abrir no dibuja nada.
TEST(MenuBarTest, ItDrawsTheOrnamentAndThenTheButtons)
{
    MenuBar bar(LabSizes());
    Client::Render::SpriteBatch closed;
    closed.Begin(Screen640);
    bar.Draw(closed, Screen640);
    EXPECT_TRUE(closed.Vertices().empty());

    bar.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(Client::Render::BlendMode::Additive);
    bar.Draw(batch, Screen640);
    EXPECT_EQ(QuadSlots(batch), (std::vector<std::uint16_t>{0x12, 0x10, 0x11}));
    for (const auto& command : batch.Commands())
    {
        EXPECT_EQ(command.blend, Client::Render::BlendMode::Alpha);
    }
    // El adorno, sin cuadros: las UV a medio téxel de su esquina.
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 3u * 6u);
    EXPECT_FLOAT_EQ(vertices[0].u, 0.5f / 256.0f);
    EXPECT_FLOAT_EQ(vertices[0].v, 0.5f / 128.0f);
}

// Como los de servidor: el cuadro 0 normal, el 1 con el mouse encima y el 2 apretado.
TEST(MenuBarTest, TheButtonsShowTheMouse)
{
    MenuBar bar(LabSizes());
    bar.Open();
    const MenuBarLayout layout = LayoutMenuBar(Screen640);
    InputState input;
    Client::Tests::MoveMouse(input, MiddleX(layout.menu), MiddleY(layout.menu));
    bar.Step(input, Screen640);

    Client::Render::SpriteBatch hover;
    hover.Begin(Screen640);
    bar.Draw(hover, Screen640);
    EXPECT_FLOAT_EQ(hover.Vertices()[6].v, (30.0f + 0.5f) / 128.0f);
    EXPECT_FLOAT_EQ(hover.Vertices()[12].v, 0.5f / 128.0f);

    input.ConsumeEdges();
    Client::Tests::MoveMouse(input, MiddleX(layout.credit), MiddleY(layout.credit));
    Client::Tests::PressLeft(input);
    bar.Step(input, Screen640);
    Client::Render::SpriteBatch pressed;
    pressed.Begin(Screen640);
    bar.Draw(pressed, Screen640);
    EXPECT_FLOAT_EQ(pressed.Vertices()[6].v, 0.5f / 128.0f);
    EXPECT_FLOAT_EQ(pressed.Vertices()[12].v, (60.0f + 0.5f) / 128.0f);
}

// 0x6411b0: el clic de cada botón. Uno en la barra, fuera de los botones, no es de ninguno.
TEST(MenuBarTest, AClickSaysWhichButton)
{
    MenuBar bar(LabSizes());
    bar.Open();
    const MenuBarLayout layout = LayoutMenuBar(Screen640);
    EXPECT_EQ(ClickOn(bar, layout.menu), MenuBarClick::Menu);
    EXPECT_EQ(ClickOn(bar, layout.credit), MenuBarClick::Credit);
    EXPECT_EQ(ClickOn(bar, layout.bar), std::nullopt);
    EXPECT_TRUE(bar.IsOpen());
}

// Cerrada no atiende nada, y abrirla otra vez no le borra un botón apretado.
TEST(MenuBarTest, AClosedBarDoesNothingAndOpeningAgainKeepsIt)
{
    MenuBar bar(LabSizes());
    const MenuBarLayout layout = LayoutMenuBar(Screen640);
    EXPECT_EQ(ClickOn(bar, layout.menu), std::nullopt);

    bar.Open();
    InputState input;
    Client::Tests::MoveMouse(input, MiddleX(layout.menu), MiddleY(layout.menu));
    Client::Tests::PressLeft(input);
    EXPECT_EQ(bar.Step(input, Screen640), std::nullopt);
    input.ConsumeEdges();
    bar.Open();
    Client::Tests::ReleaseLeft(input);
    EXPECT_EQ(bar.Step(input, Screen640), MenuBarClick::Menu);

    bar.Close();
    EXPECT_FALSE(bar.IsOpen());
    EXPECT_EQ(ClickOn(bar, layout.credit), std::nullopt);
}
} // namespace
