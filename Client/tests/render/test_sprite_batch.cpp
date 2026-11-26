#include "Render/SpriteBatch.h"

#include "Core/PixelSize.h"
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

// The original's 2D primitives turned into vertices and spans (06 § Primitivas 2D), with the GL state they leave
// (Q-C57, Q-C75). The login numbers are those of step 1 of CL5.2, computed by hand with the formulas of 0x621b60 and
// 0x622000; they are *static* until the P-C13 trace.
namespace Client::Render
{
// For GoogleTest messages: (r, g, b, a) and the slot in hex, like the research.
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
using Client::Core::PixelSize;
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

// The two login backgrounds, 0x621b60(0x15, 0, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0) and 0x16 at x = 320, and the two
// sands, 0x622000(0x5c2, 0, 25, 640, 430, u, 0, 0.3, 0.3) and 0x622000(0x5c3, 0, 25, 640, 430, u, 0, 3, 2), with the
// clock's u at 0 (06 § El login, el dibujo).
constexpr OriginalQuad LoginLeftBackground{
    .slot = {0x15}, .x = 0, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};
constexpr OriginalQuad LoginRightBackground{
    .slot = {0x16}, .x = 320, .y = 25, .width = 320, .height = 320, .scaleSize = true, .scalePosition = true};
constexpr OriginalSkewedQuad FirstSand{
    .slot = {0x5c2}, .x = 0, .y = 25, .width = 640, .height = 430, .u = 0, .v = 0, .du = 0.3f, .dv = 0.3f};
constexpr OriginalSkewedQuad SecondSand{
    .slot = {0x5c3}, .x = 0, .y = 25, .width = 640, .height = 430, .u = 0, .v = 0, .du = 3, .dv = 2};

OriginalQuad PixelQuad(TextureSlot slot, float alpha = 0.0f)
{
    return OriginalQuad{.slot = slot, .x = 10, .y = 20, .width = 30, .height = 40, .alpha = alpha};
}

// The four (x, y, u, v), in fan order.
using Corners = std::array<std::array<float, 4>, 4>;

// If the batch does not reach that quad, the test fails and there are no vertices: a subspan beyond the batch would be
// undefined behavior.
std::span<const SpriteVertex> QuadVertices(const SpriteBatch& batch, std::size_t index)
{
    const std::span<const SpriteVertex> vertices = batch.Vertices();
    if (vertices.size() < (index + 1) * 6)
    {
        ADD_FAILURE() << "el lote tiene " << vertices.size() << " vértices: no llega al quad " << index;
        return {};
    }
    return vertices.subspan(index * 6, 6);
}

// Each quad is six vertices, the fan's triangles (0, 1, 2) and (0, 2, 3) (TheFanIsTwoTrianglesInOrder), so the four
// corners are 0, 1, 2 and 5.
std::array<SpriteVertex, 4> CornersOf(const SpriteBatch& batch, std::size_t index)
{
    const std::span<const SpriteVertex> six = QuadVertices(batch, index);
    if (six.empty())
    {
        return {};
    }
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

std::vector<Color> ColorsOf(const SpriteBatch& batch, std::size_t index)
{
    std::vector<Color> colors;
    for (const SpriteVertex& vertex : QuadVertices(batch, index))
    {
        colors.push_back(vertex.color);
    }
    return colors;
}

std::vector<Color> SixOf(Color color)
{
    return std::vector<Color>(6, color);
}

// The background, the sand and the loading images, in a new batch for `screen`.
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

// Step 1 of CL5.2, at 640 × 480: the backgrounds go from 455 to 134.99997 (H − y − h without rounding H − y; 25 scaled
// is 25.000002), and the sand from 455 to 24.999968, because the scaled 430 is 430.00003. The sand's UVs skew: a
// quarter of dv on the left.
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

// And at 1024 × 768: each axis on its own, 1.6 both. The backgrounds give integers (728 and 216), and the sand ends at
// 39.999935.
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

// Without flags the quad is in pixels; with only one, only that part is scaled: the position becomes (512, 40) and the
// size stays at 320, or the corner stays at (320, 25) and the size becomes 512.
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

// A fan of 4 is the triangles (0, 1, 2) and (0, 2, 3): the six vertices repeat corners 0 and 2, in that order. With y
// up, both wind counterclockwise.
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

// With alpha above 0, each vertex gets (1, 1, 1, alpha), even if the GL color is another.
TEST(SpriteBatchTest, AnAlphaAboveZeroIsTheVertexColor)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Quad(PixelQuad(SlotA, 0.5f));

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Color{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 0.5f}));
}

// Q-C57: the quad with alpha leaves the GL color at (1, 1, 1, 1), so the next one without alpha comes out white even
// if red was requested before.
TEST(SpriteBatchTest, AQuadWithAlphaLeavesTheColorWhite)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.Quad(PixelQuad(SlotA, 0.5f));
    batch.Quad(PixelQuad(SlotA));

    EXPECT_EQ(ColorsOf(batch, 1), SixOf(White));
}

// With alpha 0 the quad uses the current color, and leaves it as it was.
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

// The fixed function clamps the color to [0, 1] (06 § Primitivas 2D): 1.34 becomes 1 and -0.5 becomes 0, and so do a
// quad's alpha and an image's color.
TEST(SpriteBatchTest, TheColorIsClampedToOne)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.SetColor(Color{.red = 1.34f, .green = -0.5f, .blue = 0.25f, .alpha = 2.0f});
    batch.Quad(PixelQuad(SlotA));
    batch.Quad(PixelQuad(SlotA, 1.5f));
    batch.Image(UiImage{.slot = SlotA,
                        .width = 4,
                        .height = 4,
                        .textureWidth = 4,
                        .textureHeight = 4,
                        .color = Color{.red = 2.0f, .green = -1.0f, .blue = 0.5f, .alpha = 1.5f}});

    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Color{.red = 1.0f, .green = 0.0f, .blue = 0.25f, .alpha = 1.0f}));
    EXPECT_EQ(ColorsOf(batch, 1), SixOf(White));
    EXPECT_EQ(ColorsOf(batch, 2), SixOf(Color{.red = 1.0f, .green = 0.0f, .blue = 0.5f, .alpha = 1.0f}));
}

// Consecutive quads with the same texture, the same blend and the same alpha test are one span: the color goes in each
// vertex and does not split it.
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

// Another blend splits the span even if the texture is the same, and the additive one goes without alpha test; going
// back to the normal one turns it on. Requesting the same blend again does not split.
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

// The untextured quad turns the alpha test off with both blends: two in a row, one with the normal blend and one with
// the additive, have the same texture (none) and the same alpha test, and only the blend separates them.
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

// A, B, A are three spans, in that order: with the normal blend, merging the two As would change what covers what.
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

// 0x621aa0(x, y, w, h) is always scaled, in the current color and without texture: at 1024 × 768, (0, 0, 320, 240)
// is the top-left quarter (240 scaled gives 384.00003, so the bottom edge ends at 383.99997). Its UVs stay at 0.
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

// Q-C75: 0x621aa0 turns the texture and the alpha test off (0x620910(0)) and nobody turns them on until the next
// blend. A textured quad that comes right after goes without texture and without alpha test, in the same span as the
// rectangle; after SetBlend it has both again.
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

// The sand has no alpha: it uses the current color, the (0.25; 0.1; 0.1) the login sets before, and leaves it.
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

// The five loading images (0x65eeb0): the size, the slot and the place at 800 × 600 from 06 § La carga, and each
// one's power-of-2 buffer (05 § Texturas).
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

// The loading UI is not the quad of 0x621b60: it goes in 800 × 600 with y from the top, each corner by fx = W / 800 and
// fy = H / 600, and the UVs at half a texel (0x647650). At 640 × 480, fx is 0.79999995: the right image ends at
// 639.99994. The math, like the x87's in step 1.
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

// And at 1024 × 768: fx = fy = 1.28 (slightly less), and the right one does reach 1024.
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

// Q-C75: the image sets its color with glColor4ub and leaves it as GL's: the next quad without alpha comes out in that
// color, not in the one requested before.
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

// Q-C75: an image without a slot turns the texture off and one with a slot turns it on; the blend and the alpha test
// stay as they were. After one without a slot, the quad comes out untextured. And Q-C64: the last quad is from slot A,
// the last one 0x620780 bound, and the image bound B without going through it; the original does not bind A again and
// draws it with B, and MuRet draws it with its own.
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

// 0x647650 does not touch the alpha test: with the normal blend the image goes with it, and after the untextured quad
// it goes without it, even though it turns the texture on.
TEST(SpriteBatchTest, AnImageKeepsTheAlphaTest)
{
    const UiImage image{.slot = SlotA, .width = 4, .height = 4, .textureWidth = 4, .textureHeight = 4};
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.Image(image);
    batch.ColorQuad(0, 0, 10, 10);
    batch.Image(image);

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].texture, SlotA);
    EXPECT_TRUE(commands[0].alphaTest);
    EXPECT_EQ(commands[2].texture, SlotA);
    EXPECT_EQ(commands[2].blend, BlendMode::Alpha);
    EXPECT_FALSE(commands[2].alphaTest);
}

// The alpha test alone also splits the span: the untextured quad turns it off, SetBlend turns it on with the same
// blend, and the image without a slot goes untextured again. Both go untextured with the normal blend, but one
// discards an alpha of 0.25 or less and the other does not.
TEST(SpriteBatchTest, AnotherAlphaTestStartsACommand)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.ColorQuad(0, 0, 10, 10);
    batch.SetBlend(BlendMode::Alpha);
    batch.Image(UiImage{.slot = std::nullopt, .width = 4, .height = 4});

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].texture, std::nullopt);
    EXPECT_EQ(commands[1].texture, std::nullopt);
    EXPECT_EQ(commands[0].blend, commands[1].blend);
    EXPECT_FALSE(commands[0].alphaTest);
    EXPECT_TRUE(commands[1].alphaTest);
    EXPECT_EQ(commands[1].firstVertex, 6u);
}

// Begin empties the frame and takes the new screen, but the GL state carries over to the next frame as in the
// original: the color, the blend and the texture off.
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

// A new batch starts like the login (06 § El login, el dibujo): normal blend with alpha test, texture on and white.
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

// 0x620980: no blend and no alpha test, with the texture on. The game's bottom bar (0x5a9290).
TEST(SpriteBatchTest, TheOpaqueBlendKeepsTheTextureAndTurnsTheAlphaTestOff)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    EXPECT_EQ(batch.Blend(), BlendMode::Alpha);

    batch.SetBlend(BlendMode::Opaque);
    batch.Quad(PixelQuad(SlotA));

    EXPECT_EQ(batch.Blend(), BlendMode::Opaque);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].blend, BlendMode::Opaque);
    EXPECT_EQ(batch.Commands()[0].texture, SlotA);
    EXPECT_FALSE(batch.Commands()[0].alphaTest);
}

// A bare glEnable(GL_TEXTURE_2D) (0x5544ed): after a rectangle, the texture comes back without the alpha test and
// without changing the blend.
TEST(SpriteBatchTest, EnablingTheTextureLeavesTheAlphaTestOff)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(BlendMode::Additive);

    batch.ColorQuad(0, 0, 10, 10);
    batch.EnableTexture();
    batch.Quad(PixelQuad(SlotA));

    const std::span<const DrawCommand> commands = batch.Commands();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[1].texture, SlotA);
    EXPECT_FALSE(commands[1].alphaTest);
    EXPECT_EQ(commands[1].blend, BlendMode::Additive);
    EXPECT_EQ(batch.Blend(), BlendMode::Additive);
}

// 0x621cc0(slot, 100, 200, 24, 24, degrees): centered at (100, 200) of 640 × 480, always scaled, with the UVs
// (0, 0), (0, 1), (1, 1) and (1, 0). Without rotation the corners are (−w/2, h/2), (−w/2, −h/2), (w/2, −h/2) and
// (w/2, h/2) around the center, with GL's y.
TEST(SpriteBatchTest, ARotatedQuadIsCenteredAndScaled)
{
    SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetColor(Red);

    batch.RotatedQuad(SlotA, 100, 200, 24, 24, 0);

    ExpectCorners(batch, 0, Corners{{{88, 292, 0, 0}, {88, 268, 0, 1}, {112, 268, 1, 1}, {112, 292, 1, 0}}});
    EXPECT_EQ(ColorsOf(batch, 0), SixOf(Red));
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].texture, SlotA);

    batch.Begin(Screen1024);
    batch.RotatedQuad(SlotA, 100, 200, 24, 24, 0);
    const std::array<SpriteVertex, 4> scaled = CornersOf(batch, 0);
    EXPECT_NEAR(scaled[0].x, 160.0f - 19.2f, 1e-4f);
    EXPECT_NEAR(scaled[0].y, 768.0f - 320.0f + 19.2f, 1e-4f);
    EXPECT_NEAR(scaled[2].x, 160.0f + 19.2f, 1e-4f);
    EXPECT_NEAR(scaled[2].y, 768.0f - 320.0f - 19.2f, 1e-4f);
}

// The rotation of 0x5fc8f0 with (0, 0, degrees) and 0x5fcbd0 is counterclockwise on the GL screen: at 90°, corner 0
// goes where 1 was, and so on; the UVs follow their corner.
TEST(SpriteBatchTest, ARotatedQuadTurnsAroundItsCenter)
{
    SpriteBatch batch;
    batch.Begin(Screen640);

    batch.RotatedQuad(SlotA, 100, 200, 24, 24, 90);

    const std::array<SpriteVertex, 4> corners = CornersOf(batch, 0);
    constexpr std::array<std::array<float, 2>, 4> Turned{{{88, 268}, {112, 268}, {112, 292}, {88, 292}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_NEAR(corners[i].x, Turned[i][0], 1e-4f);
        EXPECT_NEAR(corners[i].y, Turned[i][1], 1e-4f);
    }
    EXPECT_EQ(corners[1].v, 1.0f);
    EXPECT_EQ(corners[3].u, 1.0f);
}
} // namespace
