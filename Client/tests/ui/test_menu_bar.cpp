#include "Ui/MenuBar.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Ui/Widget.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

// Client::Ui::MenuBar: the login's bottom bar (+0xe40: 0x641000, 0x65f300, 0x6410c0, 0x641440 and 0x6411b0). Two
// 54 × 30 buttons with three frames, the menu on the left and the credits on the right, inside the deco.tga ornament.
// All in pixels.
namespace
{
using Client::Core::PixelSize;
using Client::Input::InputState;
using Client::Ui::LayoutMenuBar;
using Client::Ui::MenuBar;
using Client::Ui::MenuBarClick;
using Client::Ui::MenuBarLayout;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};

// The lab's textures: server_menu_b_all and server_credit_b_all are 54 × 90 (three frames of 30), in 64 × 128
// buffers, and deco.tga, 189 × 103, in a 256 × 128 one.
Client::Ui::TextureSizes LabSizes()
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

// 0x65f300: y = −11 − (int) (H · −0.945f) − 30. In 640 × 480, −11 + 453 − 30 = 412; the menu at the corner, the
// credits at 30 + 580 − 54 = 556 and the ornament with its point (105, 59) at their corner.
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

// The float at 0x6baae0 is a bit smaller than 0.945 in absolute value, and the product is not rounded to float: with
// 600, 600 · 0.94499999… = 566.99999… and _ftol gives 566, so the y is 525 (526 with an exact 0.945 or with the
// product in float). Same with 800: 714 and not 715.
TEST(MenuBarTest, TheHeightIsMultipliedInFullPrecision)
{
    EXPECT_EQ(LayoutMenuBar(PixelSize{800, 600}).bar.y, 525);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1280, 800}).bar.y, 714);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1280, 1024}).bar.y, 926);
    EXPECT_EQ(LayoutMenuBar(PixelSize{1600, 1200}).bar.y, 1092);
}

// 0x641440: the ornament and then the two buttons, with the normal blend even if the batch arrives with the additive
// one (Q-C80). Not open, it draws nothing.
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
    // The ornament, without frames: the UVs half a texel from its corner.
    const auto vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 3u * 6u);
    EXPECT_FLOAT_EQ(vertices[0].u, 0.5f / 256.0f);
    EXPECT_FLOAT_EQ(vertices[0].v, 0.5f / 128.0f);
}

// Like the server ones: frame 0 normal, 1 hovered and 2 pressed.
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

// 0x6411b0: each button's click. One on the bar, outside the buttons, belongs to none.
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

// Opening it again does not clear a pressed button.
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
