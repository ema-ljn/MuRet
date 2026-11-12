#include "UI/HudPrimitives.h"

#include "App/WindowState.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Las primitivas del HUD del juego (CL8 hito 2): las cifras de FontTest (0x6218f0), las barras chicas (0x5a6af0) y
// la ayuda con marco (0x554340). Las cifras y la barra son las del frame 1 de Lorencia en 1024 × 768 (GLIntercept,
// fuera del repo): el AG 17749 (líneas 124429..124469) y la barra del ayudante (124272..124299).
namespace
{
using Client::App::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr float TraceTolerance = 2e-6f;

// Las esquinas 0 y 2 del quad `index` (arriba a la izquierda y abajo a la derecha, con la y de GL).
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

// El tramo que tiene al quad `index`.
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

// El escalado de 0x621b60 deja restos de float aun en 640 × 480 (el 430 escalado es 430,00003): un poco de margen.
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

// 0x6218f0(571, 467, 17749, 9, 10): la x corre 5 · 9 · 0,5 y cada cifra avanza 9 · 0,7 = 6,3, como la traza.
TEST(HudPrimitivesTest, TheNumberIsTheAgOfTheTrace)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    const float end = Client::UI::DrawNumber(sprites, 571.0f, 467.0f, 17749, 9.0f, 10.0f);

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
    EXPECT_EQ(sprites.Commands()[0].texture, Client::UI::DigitsSlot);
    // La x del final: el avance es 9 · 0,7f guardado en float (6,2999997), sumado en float cifra a cifra.
    const auto advance = static_cast<float>(9.0 * static_cast<double>(0.7f));
    float at = 548.5f;
    for (int i = 0; i < 5; ++i)
        at = static_cast<float>(static_cast<double>(at) + static_cast<double>(advance));
    EXPECT_EQ(end, at);
}

// El '-' de un negativo es la cifra −3 del atlas (u = −0,1875), como en el original.
TEST(HudPrimitivesTest, TheMinusIsTheDigitMinusThree)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    static_cast<void>(Client::UI::DrawNumber(sprites, 100.0f, 50.0f, -12, 8.0f, 9.0f));

    ASSERT_EQ(sprites.Vertices().size(), 18u);
    EXPECT_EQ(CornersOf(sprites, 0).first.u, -0.1875f);
    EXPECT_EQ(CornersOf(sprites, 0).first.x, 100.0f - 12.0f);
    EXPECT_EQ(CornersOf(sprites, 1).first.u, 0.0625f);
    EXPECT_EQ(CornersOf(sprites, 2).first.u, 0.125f);
}

// 0x5a6af0(575, 16, 50, 2, 48, verde, acotar) en 1024 × 768: la sombra, el marco aditivo, el fondo y lo lleno, con
// los colores de la traza, y termina sin mezcla.
TEST(HudPrimitivesTest, TheGaugeIsTheHelperOfTheTrace)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    Client::UI::DrawGauge(sprites, 575.0f, 16.0f, 50.0f, 2.0f, 48.0f, false, true);

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

// La roja de la vida del invocado: el marco (0,2; 0; 0), el fondo (50, 10, 0)/255 y lo lleno (200, 50, 0)/255.
TEST(HudPrimitivesTest, TheRedGaugeHasItsColors)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::UI::DrawGauge(sprites, 10.0f, 10.0f, 50.0f, 2.0f, 25.0f, true, false);

    ASSERT_EQ(sprites.Vertices().size(), 24u);
    EXPECT_EQ(sprites.Vertices()[6].color, (Color{0.2f, 0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(sprites.Vertices()[12].color, (Color{50.0f / 255.0f, 10.0f / 255.0f, 0.0f, 1.0f}));
    EXPECT_EQ(sprites.Vertices()[18].color, (Color{200.0f / 255.0f, 50.0f / 255.0f, 0.0f, 1.0f}));
    // Lo lleno mide `fill` desde x + 2.
    ExpectRectangle(sprites, 3, {12.0f, 480.0f - 12.0f, 37.0f, 480.0f - 14.0f});
}

// Con `keepOnScreen`, la barra entra en (0..640, 0..433): lo negativo va a 0 y lo que pasa del borde queda a w + 5
// (o h + 5) de él. Sin la bandera, queda donde se pidió.
TEST(HudPrimitivesTest, TheGaugeStaysOnTheScreen)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::UI::DrawGauge(sprites, -5.0f, 430.0f, 50.0f, 2.0f, 0.0f, false, true);
    // El marco (el quad 1) está en (x, y).
    ExpectRectangle(sprites, 1, {0.0f, 480.0f - 426.0f, 54.0f, 480.0f - 432.0f});

    sprites.Begin(Screen640);
    Client::UI::DrawGauge(sprites, 600.0f, -3.0f, 50.0f, 2.0f, 0.0f, false, true);
    ExpectRectangle(sprites, 1, {585.0f, 480.0f, 639.0f, 474.0f});

    sprites.Begin(Screen640);
    Client::UI::DrawGauge(sprites, 600.0f, 430.0f, 50.0f, 2.0f, 0.0f, false, false);
    ExpectRectangle(sprites, 1, {600.0f, 50.0f, 654.0f, 44.0f});
}

// 0x554340(100, 200, "abcd") en 640 × 480 con 6 × 13 por letra: el marco negro de un punto alrededor de (98, 197,
// 28, 17), el fondo con alfa 0,8 en (99, 198, 26, 15), el texto blanco sobre transparente y la mezcla de antes.
TEST(HudPrimitivesTest, TheHelpHasItsFrameAndGivesTheBlendBack)
{
    SpriteBatch sprites;
    sprites.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(sprites);
    sprites.SetBlend(BlendMode::Additive);

    Client::UI::DrawHelp(sprites, text, 100, 200, "abcd");

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

    // El texto sale en blanco, con la textura prendida y el alpha test todavía apagado por los quads sin textura.
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

// El texto se mide en píxeles y el marco va en 640 × 480: en 1024 × 768, "abcd" mide 24 / 1,6 = 15.
TEST(HudPrimitivesTest, TheHelpMeasuresInPixels)
{
    SpriteBatch sprites;
    sprites.Begin(Screen1024);
    Client::Tests::RecordingTextDrawer text(sprites);

    Client::UI::DrawHelp(sprites, text, 100, 200, "abcd");

    // El fondo: (99, 198, 15 + 2, 13 / 1,6 + 2), escalado.
    const Corners background = CornersOf(sprites, 4);
    EXPECT_NEAR(background.first.x, 99.0f * 1.6f, 1e-4f);
    EXPECT_NEAR(background.third.x - background.first.x, 17.0f * 1.6f, 1e-4f);
    EXPECT_NEAR(background.first.y - background.third.y, (13.0f / 1.6f + 2.0f) * 1.6f, 1e-4f);
    EXPECT_EQ(sprites.Blend(), BlendMode::Alpha);
}
} // namespace
