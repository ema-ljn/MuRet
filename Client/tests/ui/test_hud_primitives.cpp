#include "Ui/HudPrimitives.h"

#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// The game HUD's primitives (CL8 milestone 2): FontTest's digits (0x6218f0), the small gauges (0x5a6af0) and the
// framed help (0x554340). The digits and the gauge are those of Lorencia's frame 1 in 1024 × 768 (GLIntercept,
// outside the repo): the AG 17749 (lines 124429..124469) and the helper's gauge (124272..124299).
namespace
{
using Client::Core::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr float TraceTolerance = 2e-6f;

// Corners 0 and 2 of quad `index` (top left and bottom right, with GL's y).
struct Corners
{
    SpriteVertex first;
    SpriteVertex third;
};

Corners CornersOf(const SpriteBatch& sprites, std::size_t index)
{
    const std::span<const SpriteVertex> vertices = sprites.Vertices();
    if (vertices.size() < (index + 1) * 6)
    {
        ADD_FAILURE() << "el lote no llega al quad " << index;
        return {};
    }
    return {vertices[index * 6], vertices[index * 6 + 2]};
}

// The command that holds quad `index`.
std::optional<DrawCommand> CommandOf(const SpriteBatch& sprites, std::size_t index)
{
    for (const DrawCommand& command : sprites.Commands())
    {
        if (index * 6 >= command.firstVertex && index * 6 < command.firstVertex + command.vertexCount)
            return command;
    }
    ADD_FAILURE() << "ningún tramo tiene al quad " << index;
    return std::nullopt;
}

// 0x621b60's scaling leaves float residue even in 640 × 480 (430 scaled is 430.00003): a little slack.
constexpr float ScaleTolerance = 1e-4f;

void ExpectRectangle(const SpriteBatch& sprites, std::size_t index, std::array<float, 4> expected,
                     float tolerance = ScaleTolerance)
{
    SCOPED_TRACE(testing::Message() << "el quad " << index);
    const Corners corners = CornersOf(sprites, index);
    EXPECT_NEAR(corners.first.x, expected[0], tolerance);
    EXPECT_NEAR(corners.first.y, expected[1], tolerance);
    EXPECT_NEAR(corners.third.x, expected[2], tolerance);
    EXPECT_NEAR(corners.third.y, expected[3], tolerance);
}

// 0x6218f0(571, 467, 17749, 9, 10): the x shifts by 5 · 9 · 0.5 and each digit advances 9 · 0.7 = 6.3, as in the
// trace.
TEST(HudPrimitivesTest, TheNumberIsTheAgOfTheTrace)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    const float end = Client::Ui::DrawNumber(sprites, 571.0f, 467.0f, 17749, 9.0f, 10.0f);

    constexpr std::array<std::array<float, 3>, 5> Trace{{{877.600037f, 892.000061f, 0.0625f},
                                                         {887.679993f, 902.080017f, 0.4375f},
                                                         {897.759949f, 912.159973f, 0.4375f},
                                                         {907.839966f, 922.239990f, 0.25f},
                                                         {917.919922f, 932.319946f, 0.5625f}}};
    ASSERT_EQ(sprites.Vertices().size(), Trace.size() * 6);
    for (std::size_t i = 0; i < Trace.size(); ++i)
    {
        ExpectRectangle(sprites, i, {Trace[i][0], 20.799988f, Trace[i][1], 4.799988f}, TraceTolerance);
        const Corners corners = CornersOf(sprites, i);
        EXPECT_EQ(corners.first.u, Trace[i][2]);
        EXPECT_EQ(corners.first.v, 0.0f);
        EXPECT_EQ(corners.third.u, Trace[i][2] + 0.0625f);
        EXPECT_EQ(corners.third.v, 0.5f);
    }
    ASSERT_EQ(sprites.Commands().size(), 1u);
    EXPECT_EQ(sprites.Commands()[0].texture, Client::Ui::DigitsSlot);
    // The final x: the advance is 9 · 0.7f stored as float (6.2999997), summed in float digit by digit.
    const auto advance = static_cast<float>(9.0 * static_cast<double>(0.7f));
    float at = 548.5f;
    for (int i = 0; i < 5; ++i)
        at = static_cast<float>(static_cast<double>(at) + static_cast<double>(advance));
    EXPECT_EQ(end, at);
}

// A negative's '-' is the atlas's digit −3 (u = −0.1875), as in the original.
TEST(HudPrimitivesTest, TheMinusIsTheDigitMinusThree)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    static_cast<void>(Client::Ui::DrawNumber(sprites, 100.0f, 50.0f, -12, 8.0f, 9.0f));

    ASSERT_EQ(sprites.Vertices().size(), 18u);
    EXPECT_EQ(CornersOf(sprites, 0).first.u, -0.1875f);
    EXPECT_EQ(CornersOf(sprites, 0).first.x, 100.0f - 12.0f);
    EXPECT_EQ(CornersOf(sprites, 1).first.u, 0.0625f);
    EXPECT_EQ(CornersOf(sprites, 2).first.u, 0.125f);
}

// 0x5a6af0(575, 16, 50, 2, 48, green, clamp) in 1024 × 768: the shadow, the additive frame, the background and the
// fill, with the trace's colors, and it ends without blending.
TEST(HudPrimitivesTest, TheGaugeIsTheHelperOfTheTrace)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    Client::Ui::DrawGauge(sprites, 575.0f, 16.0f, 50.0f, 2.0f, 48.0f, false, true);

    ASSERT_EQ(sprites.Vertices().size(), 24u);
    ExpectRectangle(sprites, 0, {921.600037f, 740.799988f, 1008.000061f, 731.200012f}, TraceTolerance);
    ExpectRectangle(sprites, 1, {920.0f, 742.400024f, 1006.400024f, 732.799988f}, TraceTolerance);
    ExpectRectangle(sprites, 2, {923.200012f, 739.200012f, 1003.200012f, 736.0f}, TraceTolerance);
    ExpectRectangle(sprites, 3, {923.200012f, 739.200012f, 1000.0f, 736.0f}, TraceTolerance);
    const std::array<Color, 4> colors{Color{0.0f, 0.0f, 0.0f, 0.5f}, Color{0.0f, 0.2f, 0.2f, 1.0f},
                                      Color{0.0f, 50.0f / 255.0f, 50.0f / 255.0f, 1.0f},
                                      Color{0.0f, 200.0f / 255.0f, 50.0f / 255.0f, 1.0f}};
    const std::array<BlendMode, 4> blends{BlendMode::Alpha, BlendMode::Additive, BlendMode::Additive,
                                          BlendMode::Additive};
    for (std::size_t i = 0; i < colors.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(sprites.Vertices()[i * 6].color, colors[i]);
        const auto command = CommandOf(sprites, i);
        ASSERT_TRUE(command.has_value());
        EXPECT_EQ(command->blend, blends[i]);
        EXPECT_EQ(command->texture, std::nullopt);
    }
    EXPECT_EQ(sprites.Blend(), BlendMode::Opaque);
}

// The summon's red life gauge: the frame (0.2, 0, 0), the background (50, 10, 0)/255 and the fill (200, 50, 0)/255.
TEST(HudPrimitivesTest, TheRedGaugeHasItsColors)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::Ui::DrawGauge(sprites, 10.0f, 10.0f, 50.0f, 2.0f, 25.0f, true, false);

    ASSERT_EQ(sprites.Vertices().size(), 24u);
    EXPECT_EQ(sprites.Vertices()[6].color, (Color{0.2f, 0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(sprites.Vertices()[12].color, (Color{50.0f / 255.0f, 10.0f / 255.0f, 0.0f, 1.0f}));
    EXPECT_EQ(sprites.Vertices()[18].color, (Color{200.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f}));
    // The fill is `fill` wide from x + 2.
    ExpectRectangle(sprites, 3, {12.0f, 480.0f - 12.0f, 37.0f, 480.0f - 14.0f});
}

// With `keepOnScreen`, the gauge fits in (0..640, 0..433): a negative goes to 0 and what passes the edge ends w + 5
// (or h + 5) from it. Without the flag, it stays where it was asked.
TEST(HudPrimitivesTest, TheGaugeStaysOnTheScreen)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::Ui::DrawGauge(sprites, -5.0f, 430.0f, 50.0f, 2.0f, 0.0f, false, true);
    // The frame (quad 1) is at (x, y).
    ExpectRectangle(sprites, 1, {0.0f, 480.0f - 426.0f, 54.0f, 480.0f - 432.0f});

    sprites.Begin(Screen640);
    Client::Ui::DrawGauge(sprites, 600.0f, -3.0f, 50.0f, 2.0f, 0.0f, false, true);
    ExpectRectangle(sprites, 1, {585.0f, 480.0f, 639.0f, 474.0f});

    sprites.Begin(Screen640);
    Client::Ui::DrawGauge(sprites, 600.0f, 430.0f, 50.0f, 2.0f, 0.0f, false, false);
    ExpectRectangle(sprites, 1, {600.0f, 50.0f, 654.0f, 44.0f});
}

// 0x554340(100, 200, "abcd") in 640 × 480 with 6 × 13 per letter: the one-point black frame around (98, 197, 28, 17),
// the background with alpha 0.8 at (99, 198, 26, 15), the white text on transparent and the previous blend.
TEST(HudPrimitivesTest, TheHelpHasItsFrameAndGivesTheBlendBack)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(sprites);
    sprites.SetBlend(BlendMode::Additive);

    Client::Ui::DrawHelp(sprites, text, 100, 200, "abcd");

    ASSERT_EQ(sprites.Vertices().size(), 6u * 6);
    ExpectRectangle(sprites, 0, {98.0f, 283.0f, 126.0f, 282.0f});
    ExpectRectangle(sprites, 1, {98.0f, 283.0f, 99.0f, 266.0f});
    ExpectRectangle(sprites, 2, {125.0f, 283.0f, 126.0f, 266.0f});
    ExpectRectangle(sprites, 3, {98.0f, 267.0f, 126.0f, 266.0f});
    ExpectRectangle(sprites, 4, {99.0f, 282.0f, 125.0f, 267.0f});
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(sprites.Vertices()[i * 6].color, (Color{0.0f, 0.0f, 0.0f, 1.0f})) << i;
    EXPECT_EQ(sprites.Vertices()[4 * 6].color, (Color{0.0f, 0.0f, 0.0f, 0.8f}));
    const auto background = CommandOf(sprites, 4);
    ASSERT_TRUE(background.has_value());
    EXPECT_EQ(background->blend, BlendMode::Alpha);
    EXPECT_EQ(background->texture, std::nullopt);

    // The text comes out white, with the texture on and the alpha test still off from the untextured quads.
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].x, 100);
    EXPECT_EQ(text.texts[0].y, 200);
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(sprites.Vertices()[5 * 6].color, Color{});
    const auto marker = CommandOf(sprites, 5);
    ASSERT_TRUE(marker.has_value());
    EXPECT_EQ(marker->texture, Client::Tests::TextMarkerSlot);
    EXPECT_FALSE(marker->alphaTest);
    EXPECT_EQ(sprites.Blend(), BlendMode::Additive);
}

// The text is measured in pixels and the frame goes in 640 × 480: in 1024 × 768, "abcd" measures 24 / 1.6 = 15.
TEST(HudPrimitivesTest, TheHelpMeasuresInPixels)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    Client::Tests::RecordingTextDrawer text(sprites);

    Client::Ui::DrawHelp(sprites, text, 100, 200, "abcd");

    // The background: (99, 198, 15 + 2, 13 / 1.6 + 2), scaled.
    const Corners background = CornersOf(sprites, 4);
    EXPECT_NEAR(background.first.x, 99.0f * 1.6f, 1e-4f);
    EXPECT_NEAR(background.third.x - background.first.x, 17.0f * 1.6f, 1e-4f);
    EXPECT_NEAR(background.first.y - background.third.y, (13.0f / 1.6f + 2.0f) * 1.6f, 1e-4f);
    EXPECT_EQ(sprites.Blend(), BlendMode::Alpha);
}
} // namespace
