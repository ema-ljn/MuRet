#include "Render/SpriteBatch.h"

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <vector>

// Client::Render::SpriteBatch: las primitivas 2D del original pasadas a vértices y tramos (06 §
// Primitivas 2D), con el estado de GL que dejan (Q-C57, Q-C75). Los números del login son los del
// paso 1 de la CL5.2, calculados a mano con las fórmulas de 0x621b60 y 0x622000; son *estáticos*
// hasta la traza de P-C13.
namespace Client::Render
{
// Para los mensajes de GoogleTest: (r, g, b, a) y el slot en hexadecimal, como el research.
void PrintTo(const Color& color, std::ostream* out)
{
    *out << "(" << color.red << ", " << color.green << ", " << color.blue << ", " << color.alpha << ")";
}

void PrintTo(const TextureSlot& slot, std::ostream* out)
{
    *out << std::format("{:#x}", slot.value);
}
} // namespace Client::Render

namespace
{
using Client::App::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DesignScale;
using Client::Render::DrawCommand;
using Client::Render::OriginalQuad;
using Client::Render::OriginalSkewedQuad;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::UiImage;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

constexpr Color White{};
constexpr Color Red{.red = 1.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f};

constexpr TextureSlot SlotA{0x15};
constexpr TextureSlot SlotB{0x16};

// Los dos fondos del login, 0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0) y el 0x16 en
// x = 320, y las dos arenas, 0x622000(0x5c2, 0, 25, 640, 430, u, 0, 0,3, 0,3) y
// 0x622000(0x5c3, 0, 25, 640, 430, u, 0, 3, 2), con el u del reloj en 0 (06 § El login, el dibujo).
constexpr OriginalQuad LoginLeftBackground{
    .slot = {0x15}, .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};
constexpr OriginalQuad LoginRightBackground{
    .slot = {0x16}, .x = 320, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};
constexpr OriginalSkewedQuad FirstSand{
    .slot = {0x5c2}, .x = 0, .y = 25, .width = 640, .height = 430, .u = 0, .v = 0, .du = 0.3f, .dv = 0.3f};
constexpr OriginalSkewedQuad SecondSand{
    .slot = {0x5c3}, .x = 0, .y = 25, .width = 640, .height = 430, .u = 0, .v = 0, .du = 3, .dv = 2};

// Un quad cualquiera en píxeles, con su slot.
OriginalQuad PixelQuad(TextureSlot slot, float alpha = 0.0f)
{
    return OriginalQuad{.slot = slot, .x = 10, .y = 20, .width = 30, .height = 40, .alpha = alpha};
}

// Las cuatro de (x, y, u, v), en el orden del fan.
using Corners = std::array<std::array<float, 4>, 4>;

// Las esquinas del quad `index` del lote: cada uno son seis vértices, los triángulos (0, 1, 2) y
// (0, 2, 3) del fan (TheFanIsTwoTrianglesInOrder), así que las cuatro son el 0, el 1, el 2 y el 5.
std::array<SpriteVertex, 4> CornersOf(const SpriteBatch& batch, std::size_t index)
{
    const std::span<const SpriteVertex> six = batch.Vertices().subspan(index * 6, 6);
    return {six[0], six[1], six[2], six[5]};
}

void ExpectCorners(const SpriteBatch& batch, std::size_t index, const Corners& expected)
{
    const std::array<SpriteVertex, 4> corners = CornersOf(batch, index);
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        SCOPED_TRACE(testing::Message() << "el quad " << index << ", la esquina " << i);
        EXPECT_EQ(corners[i].x, expected[i][0]);
        EXPECT_EQ(corners[i].y, expected[i][1]);
        EXPECT_EQ(corners[i].u, expected[i][2]);
        EXPECT_EQ(corners[i].v, expected[i][3]);
    }
}

// Los colores de los seis vértices del quad `index`.
std::vector<Color> ColorsOf(const SpriteBatch& batch, std::size_t index)
{
    std::vector<Color> colors;
    for (const SpriteVertex& vertex : batch.Vertices().subspan(index * 6, 6))
    {
        colors.push_back(vertex.color);
    }
    return colors;
}

std::vector<Color> SixOf(Color color)
{
    return std::vector<Color>(6, color);
}

// El fondo, la arena y las imágenes de la carga, en un lote nuevo de `screen`.
SpriteBatch LoginBatch(PixelSize screen)
{
    SpriteBatch batch;
    batch.Begin(screen);
    batch.Quad(LoginLeftBackground);
    batch.Quad(LoginRightBackground);
    batch.SetBlend(BlendMode::Additive);
    batch.SkewedQuad(FirstSand);
    batch.SkewedQuad(SecondSand);
    return batch;
}

// Paso 1 de la CL5.2, en 640 × 480: los fondos van de 455 a 134,99997 (H − y − h sin redondear
// H − y; 25 escalado es 25,000002), y la arena, de 455 a 24,999968, porque el 430 escalado es
// 430,00003. Las UV de la arena se inclinan: un cuarto de dv a la izquierda.
TEST(SpriteBatchTest, PlacesTheLoginBackgroundLikeTheOriginal)
{
    const SpriteBatch batch = LoginBatch(Screen640);

    ASSERT_EQ(batch.Vertices().size(), 4u * 6u);
    ExpectCorners(
        batch, 0,
        Corners{{{0, 455, 0, 0}, {0, 134.99996948242188f, 0, 1}, {320, 134.99996948242188f, 1, 1}, {320, 455, 1, 0}}});
    ExpectCorners(
        batch, 1,
        Corners{
            {{320, 455, 0, 0}, {320, 134.99996948242188f, 0, 1}, {640, 134.99996948242188f, 1, 1}, {640, 455, 1, 0}}});
    ExpectCorners(batch, 2,
                  Corners{{{0, 455, 0, 0.07500000298023224f},
                           {0, 24.999967575073242f, 0, 0.22500000894069672f},
                           {640, 24.999967575073242f, 0.30000001192092896f, 0.30000001192092896f},
                           {640, 455, 0.30000001192092896f, 0}}});
    ExpectCorners(batch, 3,
                  Corners{{{0, 455, 0, 0.5f},
                           {0, 24.999967575073242f, 0, 1.5f},
                           {640, 24.999967575073242f, 3, 2},
                           {640, 455, 3, 0}}});
}

// Y en 1024 × 768: cada eje por su lado, 1,6 los dos. Los fondos dan enteros (728 y 216), y la
// arena termina en 39,999935.
TEST(SpriteBatchTest, PlacesTheLoginBackgroundLikeTheOriginalIn1024x768)
{
    const SpriteBatch batch = LoginBatch(Screen1024);

    ExpectCorners(batch, 0, Corners{{{0, 728, 0, 0}, {0, 216, 0, 1}, {512, 216, 1, 1}, {512, 728, 1, 0}}});
    ExpectCorners(batch, 1, Corners{{{512, 728, 0, 0}, {512, 216, 0, 1}, {1024, 216, 1, 1}, {1024, 728, 1, 0}}});
    ExpectCorners(batch, 2,
                  Corners{{{0, 728, 0, 0.07500000298023224f},
                           {0, 39.999935150146484f, 0, 0.22500000894069672f},
                           {1024, 39.999935150146484f, 0.30000001192092896f, 0.30000001192092896f},
                           {1024, 728, 0.30000001192092896f, 0}}});
    ExpectCorners(batch, 3,
                  Corners{{{0, 728, 0, 0.5f},
                           {0, 39.999935150146484f, 0, 1.5f},
                           {1024, 39.999935150146484f, 3, 2},
                           {1024, 728, 3, 0}}});
}

// Sin banderas, el quad va en píxeles; con una sola, solo esa parte se escala: la posición pasa a
// (512, 40) y el tamaño sigue en 320, o la esquina sigue en (320, 25) y el tamaño pasa a 512.
TEST(SpriteBatchTest, WithoutTheScaleFlagsItIsInPixels)
{
    OriginalQuad quad{.slot = SlotB, .x = 320, .y = 25, .width = 320, .height = 320};
    SpriteBatch batch;
    batch.Begin(Screen1024);

    batch.Quad(quad);
    quad.scalePosition = true;
    batch.Quad(quad);
    quad.scalePosition = false;
    quad.scaleSize = true;
    batch.Quad(quad);

    ExpectCorners(batch, 0, Corners{{{320, 743, 0, 0}, {320, 423, 0, 1}, {640, 423, 1, 1}, {640, 743, 1, 0}}});
    ExpectCorners(batch, 1, Corners{{{512, 728, 0, 0}, {512, 408, 0, 1}, {832, 408, 1, 1}, {832, 728, 1, 0}}});
    ExpectCorners(batch, 2, Corners{{{320, 743, 0, 0}, {320, 231, 0, 1}, {832, 231, 1, 1}, {832, 743, 1, 0}}});
}

// Un fan de 4 son los triángulos (0, 1, 2) y (0, 2, 3): los seis vértices repiten la esquina 0 y la
// 2, en ese orden. Con la y para arriba, los dos giran al revés de las agujas del reloj.
TEST(SpriteBatchTest, TheFanIsTwoTrianglesInOrder)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Quad(PixelQuad(SlotA));

    const std::span<const SpriteVertex> vertices = batch.Vertices();
    ASSERT_EQ(vertices.size(), 6u);
    const std::array<std::array<float, 2>, 6> expected{
        {{10, 460}, {10, 420}, {40, 420}, {10, 460}, {40, 420}, {40, 460}}};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(vertices[i].x, expected[i][0]);
        EXPECT_EQ(vertices[i].y, expected[i][1]);
    }
}

// Con el alfa en más de 0, cada vértice va con (1, 1, 1, alfa), aunque el color de GL sea otro.
TEST(SpriteBatchTest, AnAlphaAboveZeroIsTheVertexColor)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Quad(PixelQuad(SlotA, 0.5f));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Color{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 0.5f}));
}

// Q-C57: el quad con alfa deja el color de GL en (1, 1, 1, 1), así que el siguiente sin alfa sale
// blanco aunque antes se haya pedido rojo.
TEST(SpriteBatchTest, AQuadWithAlphaLeavesTheColorWhite)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Quad(PixelQuad(SlotA, 0.5f));
    batch.Quad(PixelQuad(SlotA));

    EXPECT_EQ(ColorsOf(batch, 1), SixOf(White));
}

// Con alfa 0 el quad usa el color que haya, y lo deja como estaba.
TEST(SpriteBatchTest, AZeroAlphaUsesTheCurrentColor)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Quad(PixelQuad(SlotA));
    batch.Quad(PixelQuad(SlotA));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Red));
    EXPECT_EQ(ColorsOf(batch, 1), SixOf(Red));
}

// La función fija recorta el color a [0, 1] (06 § Primitivas 2D): 1,34 queda en 1 y -0,5 en 0, y el
// alfa de un quad, también.
TEST(SpriteBatchTest, TheColorIsClampedToOne)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.SetColor(Color{.red = 1.34f, .green = -0.5f, .blue = 0.25f, .alpha = 2.0f});
    batch.Quad(PixelQuad(SlotA));
    batch.Quad(PixelQuad(SlotA, 1.5f));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Color{.red = 1.0f, .green = 0.0f, .blue = 0.25f, .alpha = 1.0f}));
    EXPECT_EQ(ColorsOf(batch, 1), SixOf(White));
}

// Los quads seguidos con la misma textura, la misma mezcla y el mismo alpha test son un tramo: el
// color va en cada vértice y no corta.
TEST(SpriteBatchTest, QuadsOfTheSameTextureAreOneCommand)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Quad(PixelQuad(SlotA));
    batch.SetColor(Red);
    batch.Quad(PixelQuad(SlotA));
    batch.Quad(PixelQuad(SlotA, 0.5f));

    ASSERT_EQ(batch.Commands().size(), 1u);
    const DrawCommand& command = batch.Commands()[0];
    EXPECT_EQ(command.texture, SlotA);
    EXPECT_EQ(command.blend, BlendMode::Alpha);
    EXPECT_TRUE(command.alphaTest);
    EXPECT_EQ(command.firstVertex, 0u);
    EXPECT_EQ(command.vertexCount, 18u);
}

// Otra mezcla corta el tramo aunque la textura sea la misma, y la aditiva va sin alpha test; volver
// a la normal lo prende. Pedir la misma mezcla otra vez no corta.
TEST(SpriteBatchTest, ADifferentBlendStartsACommand)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Quad(PixelQuad(SlotA));
    batch.SetBlend(BlendMode::Alpha);
    batch.Quad(PixelQuad(SlotA));
    batch.SetBlend(BlendMode::Additive);
    batch.Quad(PixelQuad(SlotA));
    batch.SetBlend(BlendMode::Alpha);
    batch.Quad(PixelQuad(SlotA));

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].blend, BlendMode::Alpha);
    EXPECT_TRUE(commands[0].alphaTest);
    EXPECT_EQ(commands[0].vertexCount, 12u);
    EXPECT_EQ(commands[1].blend, BlendMode::Additive);
    EXPECT_FALSE(commands[1].alphaTest);
    EXPECT_EQ(commands[1].firstVertex, 12u);
    EXPECT_EQ(commands[2].blend, BlendMode::Alpha);
    EXPECT_TRUE(commands[2].alphaTest);
    EXPECT_EQ(commands[2].firstVertex, 18u);
}

// El quad sin textura apaga el alpha test con las dos mezclas: dos seguidos, uno con la normal y
// otro con la aditiva, tienen la misma textura (ninguna) y el mismo alpha test, y solo la mezcla
// los separa.
TEST(SpriteBatchTest, ADifferentBlendStartsACommandWithTheSameAlphaTest)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.ColorQuad(0, 0, 10, 10);
    batch.SetBlend(BlendMode::Additive);
    batch.ColorQuad(0, 0, 10, 10);

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].blend, BlendMode::Alpha);
    EXPECT_EQ(commands[1].blend, BlendMode::Additive);
    EXPECT_EQ(commands[0].alphaTest, commands[1].alphaTest);
}

// A, B, A son tres tramos, en ese orden: con la mezcla normal, juntar las dos A cambiaría qué tapa
// a qué.
TEST(SpriteBatchTest, TheOrderIsKept)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Quad(PixelQuad(SlotA));
    batch.Quad(PixelQuad(SlotB));
    batch.Quad(PixelQuad(SlotA));

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].texture, SlotA);
    EXPECT_EQ(commands[1].texture, SlotB);
    EXPECT_EQ(commands[2].texture, SlotA);
    EXPECT_EQ(commands[0].firstVertex, 0u);
    EXPECT_EQ(commands[1].firstVertex, 6u);
    EXPECT_EQ(commands[2].firstVertex, 12u);
    for (const DrawCommand& command : commands)
    {
        EXPECT_EQ(command.vertexCount, 6u);
    }
}

// 0x621aa0(x, y, w, h) va siempre escalado, del color que haya y sin textura: en 1024 × 768,
// (0, 0, 320, 240) es el cuarto de arriba a la izquierda (240 escalado da 384,00003, así que el
// borde de abajo queda en 383,99997). Sus UV quedan en 0.
TEST(SpriteBatchTest, AColorQuadIsScaledAndHasNoTexture)
{
    SpriteBatch batch;
    batch.Begin(Screen1024);
    batch.SetColor(Red);

    batch.ColorQuad(0, 0, 320, 240);

    ExpectCorners(
        batch, 0,
        Corners{{{0, 768, 0, 0}, {0, 383.99996948242188f, 0, 0}, {512, 383.99996948242188f, 0, 0}, {512, 768, 0, 0}}});
    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Red));
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].texture, std::nullopt);
    EXPECT_FALSE(batch.Commands()[0].alphaTest);
}

// Q-C75: 0x621aa0 apaga la textura y el alpha test (0x620910(0)) y nadie los prende hasta la
// próxima mezcla. Un quad con textura que viene justo después sale sin textura y sin alpha test, en
// el mismo tramo que el rectángulo; después de SetBlend vuelve a tener las dos.
TEST(SpriteBatchTest, AColorQuadLeavesTheTextureAndTheAlphaTestOff)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.ColorQuad(0, 0, 10, 10);
    batch.Quad(PixelQuad(SlotA));
    batch.SetBlend(BlendMode::Alpha);
    batch.Quad(PixelQuad(SlotA));

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].texture, std::nullopt);
    EXPECT_FALSE(commands[0].alphaTest);
    EXPECT_EQ(commands[0].vertexCount, 12u);
    EXPECT_EQ(commands[1].texture, SlotA);
    EXPECT_TRUE(commands[1].alphaTest);
}

// La arena no tiene alfa: usa el color que haya, el (0,25; 0,1; 0,1) que pone el login antes, y
// lo deja.
TEST(SpriteBatchTest, ASkewedQuadUsesTheCurrentColor)
{
    constexpr Color SandColor{.red = 0.25f, .green = 0.1f, .blue = 0.1f, .alpha = 1.0f};
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(BlendMode::Additive);
    batch.SetColor(SandColor);

    batch.SkewedQuad(FirstSand);
    batch.SkewedQuad(SecondSand);

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(SandColor));
    EXPECT_EQ(ColorsOf(batch, 1), SixOf(SandColor));
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].texture, TextureSlot{0x5c2});
    EXPECT_EQ(batch.Commands()[1].texture, TextureSlot{0x5c3});
    EXPECT_EQ(batch.Commands()[1].blend, BlendMode::Additive);
}

// Las cinco imágenes de la carga (0x65eeb0): el tamaño, el slot y el lugar en 800 × 600 de 06 §
// La carga, y el buffer de lados potencia de 2 de cada una (05 § Texturas).
struct LoadingImage
{
    std::uint16_t slot = 0;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    float bufferWidth = 0.0f;
    float bufferHeight = 0.0f;
};

constexpr std::array<LoadingImage, 5> LoadingImages{{
    {0x71e, 512, 478, 0, 60, 512, 512},
    {0x71f, 288, 478, 512, 60, 512, 512},
    {0x720, 93, 48, 3, 80, 128, 64},
    {0x721, 256, 206, 544, 60, 256, 256},
    {0x722, 187, 151, 307, 225, 256, 256},
}};

SpriteBatch LoadingBatch(PixelSize screen)
{
    SpriteBatch batch;
    batch.Begin(screen);
    for (const LoadingImage& image : LoadingImages)
    {
        batch.Image(UiImage{.slot = TextureSlot{image.slot},
                            .x = image.x,
                            .y = image.y,
                            .width = image.width,
                            .height = image.height,
                            .textureWidth = image.bufferWidth,
                            .textureHeight = image.bufferHeight,
                            .scaleX = DesignScale(screen.width, 800),
                            .scaleY = DesignScale(screen.height, 600)});
    }
    return batch;
}

// La UI de la carga no es el quad de 0x621b60: va en 800 × 600 con la y desde arriba, cada
// esquina por fx = W / 800 y fy = H / 600, y las UV a medio téxel (0x647650). En 640 × 480, fx es
// 0,79999995: la imagen de la derecha termina en 639,99994. Las cuentas, como las de la x87 en el
// paso 1.
TEST(SpriteBatchTest, PlacesTheLoadingImagesLikeTheOriginal)
{
    const SpriteBatch batch = LoadingBatch(Screen640);

    ASSERT_EQ(batch.Vertices().size(), 5u * 6u);
    ExpectCorners(batch, 0,
                  Corners{{{0, 432, 0.0009765625f, 0.0009765625f},
                           {0, 49.60000228881836f, 0.0009765625f, 0.9326171875f},
                           {409.5999755859375f, 49.60000228881836f, 0.9990234375f, 0.9326171875f},
                           {409.5999755859375f, 432, 0.9990234375f, 0.0009765625f}}});
    ExpectCorners(batch, 2,
                  Corners{{{2.3999998569488525f, 416, 0.00390625f, 0.0078125f},
                           {2.3999998569488525f, 377.6000061035156f, 0.00390625f, 0.7421875f},
                           {76.79999542236328f, 377.6000061035156f, 0.72265625f, 0.7421875f},
                           {76.79999542236328f, 416, 0.72265625f, 0.0078125f}}});
    ExpectCorners(batch, 3,
                  Corners{{{435.1999816894531f, 432, 0.001953125f, 0.001953125f},
                           {435.1999816894531f, 267.20001220703125f, 0.001953125f, 0.802734375f},
                           {639.9999389648438f, 267.20001220703125f, 0.998046875f, 0.802734375f},
                           {639.9999389648438f, 432, 0.998046875f, 0.001953125f}}});
    ExpectCorners(batch, 4,
                  Corners{{{245.59999084472656f, 300, 0.001953125f, 0.001953125f},
                           {245.59999084472656f, 179.1999969482422f, 0.001953125f, 0.587890625f},
                           {395.1999816894531f, 179.1999969482422f, 0.728515625f, 0.587890625f},
                           {395.1999816894531f, 300, 0.728515625f, 0.001953125f}}});
}

// Y en 1024 × 768: fx = fy = 1,28 (un poco menos), y la de la derecha sí llega a 1024.
TEST(SpriteBatchTest, PlacesTheLoadingImagesLikeTheOriginalIn1024x768)
{
    const SpriteBatch batch = LoadingBatch(Screen1024);

    ExpectCorners(batch, 0,
                  Corners{{{0, 691.2000122070312f, 0.0009765625f, 0.0009765625f},
                           {0, 79.36000061035156f, 0.0009765625f, 0.9326171875f},
                           {655.3599853515625f, 79.36000061035156f, 0.9990234375f, 0.9326171875f},
                           {655.3599853515625f, 691.2000122070312f, 0.9990234375f, 0.0009765625f}}});
    ExpectCorners(batch, 1,
                  Corners{{{655.3599853515625f, 691.2000122070312f, 0.0009765625f, 0.0009765625f},
                           {655.3599853515625f, 79.36000061035156f, 0.0009765625f, 0.9326171875f},
                           {1024, 79.36000061035156f, 0.5615234375f, 0.9326171875f},
                           {1024, 691.2000122070312f, 0.5615234375f, 0.0009765625f}}});
    ExpectCorners(batch, 4,
                  Corners{{{392.9599914550781f, 480, 0.001953125f, 0.001953125f},
                           {392.9599914550781f, 286.7200012207031f, 0.001953125f, 0.587890625f},
                           {632.3200073242188f, 286.7200012207031f, 0.728515625f, 0.587890625f},
                           {632.3200073242188f, 480, 0.728515625f, 0.001953125f}}});
}

// Q-C75: la imagen pone su color con glColor4ub y lo deja como el de GL: el quad siguiente sin alfa
// sale de ese color, no del que se pidió antes.
TEST(SpriteBatchTest, AnImageLeavesItsColor)
{
    constexpr Color Shade{.red = 0.5f, .green = 0.5f, .blue = 1.0f, .alpha = 1.0f};
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Image(UiImage{.slot = SlotA, .width = 4, .height = 4, .textureWidth = 4, .textureHeight = 4, .color = Shade});
    batch.Quad(PixelQuad(SlotB));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Shade));
    EXPECT_EQ(ColorsOf(batch, 1), SixOf(Shade));
}

// Q-C75: una imagen sin slot apaga la textura y una con slot la prende; la mezcla y el alpha test
// quedan como estaban. Después de una sin slot, el quad sale sin textura.
TEST(SpriteBatchTest, AnImageTurnsTheTextureOnByItsSlot)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(BlendMode::Additive);

    batch.Image(UiImage{.slot = std::nullopt, .width = 4, .height = 4});
    batch.Quad(PixelQuad(SlotA));
    batch.ColorQuad(0, 0, 10, 10);
    batch.Image(UiImage{.slot = SlotB, .width = 4, .height = 4, .textureWidth = 4, .textureHeight = 4});
    batch.Quad(PixelQuad(SlotA));

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].texture, std::nullopt);
    EXPECT_EQ(commands[0].blend, BlendMode::Additive);
    EXPECT_FALSE(commands[0].alphaTest);
    EXPECT_EQ(commands[0].vertexCount, 18u);
    EXPECT_EQ(commands[1].texture, SlotB);
    EXPECT_FALSE(commands[1].alphaTest);
    EXPECT_EQ(commands[2].texture, SlotA);
    EXPECT_FALSE(commands[2].alphaTest);
}

// Begin vacía el frame y toma la pantalla nueva, pero el estado de GL pasa al frame siguiente como
// en el original: el color, la mezcla y la textura apagada.
TEST(SpriteBatchTest, BeginEmptiesTheFrameAndKeepsTheState)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(BlendMode::Additive);
    batch.SetColor(Red);
    batch.ColorQuad(0, 0, 10, 10);

    batch.Begin(Screen1024);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(batch.Screen().width, Screen1024.width);
    EXPECT_EQ(batch.Screen().height, Screen1024.height);

    batch.Quad(PixelQuad(SlotA));
    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Red));
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].texture, std::nullopt);
    EXPECT_EQ(batch.Commands()[0].blend, BlendMode::Additive);
    EXPECT_FALSE(batch.Commands()[0].alphaTest);
}

// Un lote nuevo empieza como el login (06 § El login, el dibujo): mezcla normal con alpha test,
// textura prendida y blanco.
TEST(SpriteBatchTest, StartsLikeTheLogin)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Quad(PixelQuad(SlotA));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(White));
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].texture, SlotA);
    EXPECT_EQ(batch.Commands()[0].blend, BlendMode::Alpha);
    EXPECT_TRUE(batch.Commands()[0].alphaTest);
}
} // namespace
