#include "Render/Gl/GlModelRenderer.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureEnvMode.h"
#include "Render/TextureSlot.h"

#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The camera and the square are GlDrawTest.h's: the square fills the 64 × 64 framebuffer.
namespace
{
using Client::Core::PixelSize;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::Camera3D;
using Client::Render::Color;
using Client::Render::MakePass3D;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::ModelFog;
using Client::Render::ModelVertex;
using Client::Render::OriginalQuad;
using Client::Render::Pass3D;
using Client::Render::SpriteBatch;
using Client::Render::TextureEnvMode;
using Client::Render::TextureSlot;
using Client::Render::ViewportRect;
using Client::Render::Gl::GlModelRenderer;
using Client::Render::Gl::GlSpriteRenderer;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Tests::FramebufferSide;
using Client::Tests::TestPass;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Red{255, 0, 0, 255};
constexpr Rgba8 Green{0, 255, 0, 255};

constexpr Color RedColor{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color GreenColor{0.0f, 1.0f, 0.0f, 1.0f};

// A white texel and a 50 % gray one. Neither is from an original scene.
constexpr TextureSlot WhiteSlot{0x1};
constexpr TextureSlot GraySlot{0x2};
constexpr TextureSlot MissingSlot{0x2a};

constexpr PixelSize Screen{FramebufferSide, FramebufferSide};

LoadedTexture Rgba(TextureSlot slot, std::vector<std::uint8_t> pixel)
{
    return {slot.value,
            {},
            Texture{.width = 1,
                    .height = 1,
                    .textureWidth = 1,
                    .textureHeight = 1,
                    .format = PixelFormat::Rgba,
                    .pixels = std::move(pixel)}};
}

// One unit of tolerance per channel: 8-bit blending rounds as the driver likes.
void ExpectNear(const std::optional<Rgba8>& cell, Rgba8 expected)
{
    ASSERT_TRUE(cell.has_value());
    const auto withinOne = [](std::uint8_t actual, std::uint8_t wanted) { return std::abs(actual - wanted) <= 1; };
    EXPECT_TRUE(withinOne(cell->red, expected.red) && withinOne(cell->green, expected.green) &&
                withinOne(cell->blue, expected.blue) && withinOne(cell->alpha, expected.alpha))
        << ::testing::PrintToString(*cell) << " contra " << ::testing::PrintToString(expected);
}

class GlModelRendererTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        Client::Core::Expected<GlModelRenderer, std::string> renderer = GlModelRenderer::Create(Gl());
        ASSERT_TRUE(renderer.HasValue()) << renderer.Error();
        m_models.emplace(std::move(renderer).Value());
        m_textures.emplace(Gl());
        const std::array<LoadedTexture, 2> textures{Rgba(WhiteSlot, {255, 255, 255, 255}),
                                                    Rgba(GraySlot, {128, 128, 128, 255})};
        const Client::Core::Expected<void, std::string> loaded = m_textures->Load(textures);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        m_batch.Begin(TestPass(Screen));
    }

    void AddSquare(const ModelDrawState& state, float depth, Color color, bool backFacing = false)
    {
        Client::Tests::AddSquare(m_batch, state, depth, color, backFacing);
    }

    void DrawBatch()
    {
        m_models->Draw(m_batch, *m_textures, m_log);
        ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    // A cell of a 2 × 2 grid over the framebuffer, top row first.
    [[nodiscard]] std::optional<Rgba8> Cell(int index) const
    {
        return Cells(2, 2)[static_cast<std::size_t>(index)];
    }

    std::optional<GlModelRenderer> m_models;
    std::optional<TextureTable> m_textures;
    ModelBatch m_batch;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// With 0x620db0's depth test the nearest wins each pixel, even if it comes first: red at 5 covers green at 20 drawn
// after.
TEST_F(GlModelRendererTest, DrawsATriangleWithDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    for (int cell = 0; cell < 4; ++cell)
    {
        ExpectNear(Cell(cell), Red);
    }
}

// The card portrait pass (0x495949) clears the depth on start: its batch's green at 20 covers the red at 5 the previous
// batch left.
TEST_F(GlModelRendererTest, APassThatClearsTheDepthDrawsOverTheOneBefore)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 5.0f, RedColor);
    DrawBatch();
    auto pass = TestPass(Screen);
    pass.clearDepth = true;
    m_batch.Begin(pass);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    for (int cell = 0; cell < 4; ++cell)
    {
        ExpectNear(Cell(cell), Green);
    }
}

// Flag 0x2000: without the depth test, what comes later covers.
TEST_F(GlModelRendererTest, TheDepthTestCanBeOff)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, false}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// 0x620980 leaves culling on: the back face does not show. 0x620a00(1) turns it off.
TEST_F(GlModelRendererTest, CullsTheBackFaces)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, RedColor, true);
    DrawBatch();
    EXPECT_EQ(Cell(0), Transparent);

    m_batch.Begin(TestPass(Screen));
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, GreenColor, true);
    DrawBatch();
    ExpectNear(Cell(0), Green);
}

// 0x620a90 does not write depth: the opaque green at 20, drawn after, is not hidden by the additive red at 5, and
// covers it.
TEST_F(GlModelRendererTest, TheAdditiveDoesNotWriteTheDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Additive, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// The shadow (0x620980 and 0x4eff40's mask off) neither blends nor writes depth: what is behind and after shows.
TEST_F(GlModelRendererTest, TheShadowDoesNotWriteTheDepth)
{
    AddSquare({std::nullopt, ModelBlend::Shadow, true}, 5.0f, RedColor);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// A world object's shadow (0x6002e0 with the horse: 0x620a00(1) and 0x4eff40's mask off) blends with the vertex alpha:
// black at 0.7 over white leaves 0.3.
TEST_F(GlModelRendererTest, TheBlendedShadowMixesWithItsAlpha)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.7f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{77, 77, 77, 201});
}

// And it does not write depth: the opaque green at 20, drawn after, covers it.
TEST_F(GlModelRendererTest, TheBlendedShadowDoesNotWriteTheDepth)
{
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.7f});
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// And no alpha test: 0x4eff40 turns it off (0x620910(0)), and map 7's shadow is at 0.2. Black at 0.2 over white leaves
// 0.8.
TEST_F(GlModelRendererTest, TheBlendedShadowHasNoAlphaTest)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 5.0f, Color{0.0f, 0.0f, 0.0f, 0.2f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{204, 204, 204, 214});
}

// Whirl 90 of map 55 (0x6002e0 with 0x620b10): GL_ZERO, GL_ONE_MINUS_SRC_COLOR. Over white, color (0.25; 0.5; 0.75)
// leaves its complement, (0.75; 0.5; 0.25): it darkens where the texture is bright.
TEST_F(GlModelRendererTest, TheSubtractiveDarkensWhereTheColorIsLight)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    AddSquare({WhiteSlot, ModelBlend::Subtractive, true}, 5.0f, Color{0.25f, 0.5f, 0.75f, 1.0f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{191, 128, 64, 0});
}

// And it does not write depth: the opaque green at 20, drawn after, covers it.
TEST_F(GlModelRendererTest, TheSubtractiveDoesNotWriteTheDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Subtractive, true}, 5.0f, Color{0.25f, 0.5f, 0.75f, 1.0f});
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

// Nor does it cull back faces (0x620a00 turns culling off): a back one, red at 0.7 over nothing, shows.
TEST_F(GlModelRendererTest, TheBlendedShadowDoesNotCull)
{
    AddSquare({std::nullopt, ModelBlend::BlendedShadow, true}, 10.0f, Color{1.0f, 0.0f, 0.0f, 0.7f}, true);

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{179, 0, 0, 125});
}

// Opaque does not blend: an alpha of 0.5 is written as is, without the background.
TEST_F(GlModelRendererTest, TheOpaqueIgnoresTheAlpha)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, Color{1.0f, 0.0f, 0.0f, 0.5f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{255, 0, 0, 128});
}

// The fixed function clamps the vertex color to [0, 1] before the texture: 1.34 times 50 % gray gives 128, not 171.
TEST_F(GlModelRendererTest, ClampsTheColorToOne)
{
    AddSquare({GraySlot, ModelBlend::Opaque, true}, 10.0f, Color{1.34f, 1.34f, 1.34f, 1.0f});
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{128, 128, 128, 255});

    m_batch.Begin(TestPass(Screen));
    AddSquare({std::nullopt, ModelBlend::Opaque, true}, 10.0f, Color{1.34f, 0.0f, 0.0f, 1.0f});
    DrawBatch();
    ExpectNear(Cell(0), Red);
}

// 0x620a00's glAlphaFunc(GL_GREATER, 0.25): an alpha of 0.25 fails, one of 0.3 passes.
TEST_F(GlModelRendererTest, TheAlphaTestCutsAtAQuarter)
{
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, Color{1.0f, 1.0f, 1.0f, 0.25f});
    DrawBatch();
    EXPECT_EQ(Cell(0), Transparent);

    m_batch.Begin(TestPass(Screen));
    AddSquare({WhiteSlot, ModelBlend::Alpha, true}, 10.0f, Color{1.0f, 1.0f, 1.0f, 0.3f});
    DrawBatch();
    EXPECT_NE(Cell(0), Transparent);
}

// The viewport is the pass's: the top-left rectangle of 640 × 480 fills only that quarter.
TEST_F(GlModelRendererTest, UsesTheViewportOfThePass)
{
    m_batch.Begin(TestPass(Screen, ViewportRect{0, 0, 320, 240}));
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 10.0f, RedColor);

    DrawBatch();

    ExpectNear(Cell(0), Red);
    EXPECT_EQ(Cell(1), Transparent);
    EXPECT_EQ(Cell(2), Transparent);
    EXPECT_EQ(Cell(3), Transparent);
}

// The 2D after the pass turns the depth test and culling off, like 0x6219b0 → 0x620870: the SpriteBatch quad shows
// whole even though the pass wrote a nearer depth.
TEST_F(GlModelRendererTest, TheTwoDAfterTurnsTheDepthOff)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 1.1f, RedColor);
    DrawBatch();
    Client::Core::Expected<GlSpriteRenderer, std::string> sprites = GlSpriteRenderer::Create(Gl());
    ASSERT_TRUE(sprites.HasValue()) << sprites.Error();
    SpriteBatch batch;
    batch.Begin(Screen);
    batch.SetColor(Color{0.0f, 1.0f, 0.0f, 1.0f});
    batch.Quad(OriginalQuad{.slot = WhiteSlot,
                            .x = 0.0f,
                            .y = 0.0f,
                            .width = static_cast<float>(FramebufferSide),
                            .height = static_cast<float>(FramebufferSide)});

    sprites.Value().Draw(batch, *m_textures, m_log);
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));

    for (int cell = 0; cell < 4; ++cell)
    {
        ExpectNear(Cell(cell), Green);
    }
}

// 0x42b0a0's linear fog, here black from 10 to 30: with d the eye distance (the square's depth), white ends at
// (30 − d) / 20, clamped to [0, 1]; alpha does not change. Drawn far to near so the depth test lets each one pass.
TEST_F(GlModelRendererTest, TheLinearFogMixesByTheEyeDistance)
{
    const ModelFog fog{.color = glm::vec3(0.0f), .start = 10.0f, .end = 30.0f};
    const std::array<std::pair<float, Rgba8>, 4> cases{
        std::pair{40.0f, Rgba8{0, 0, 0, 128}}, std::pair{25.0f, Rgba8{64, 64, 64, 128}},
        std::pair{20.0f, Rgba8{128, 128, 128, 128}}, std::pair{5.0f, Rgba8{255, 255, 255, 128}}};
    for (const auto& [depth, expected] : cases)
    {
        SCOPED_TRACE(depth);
        m_batch.Begin(TestPass(Screen));
        m_batch.SetFog(fog);
        AddSquare({WhiteSlot, ModelBlend::Opaque, true}, depth, Color{1.0f, 1.0f, 1.0f, 0.5f});
        DrawBatch();
        ExpectNear(Cell(0), expected);
    }
}

// Q-C391: eye |z| does not depend on the projection; in orthographic clip.w is always 1.
TEST_F(GlModelRendererTest, OrthographicFogUsesTheTransformedEyeDepth)
{
    auto pass = TestPass(Screen);
    pass.projection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 100.0f);
    pass.view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
    m_batch.Begin(pass);
    m_batch.SetFog(ModelFog{.color = glm::vec3(0.0f), .start = 10.0f, .end = 30.0f});
    AddSquare({WhiteSlot, ModelBlend::Opaque, false}, 10.0f, Color{1, 1, 1, 0.5f});
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{128, 128, 128, 128});
}

TEST_F(GlModelRendererTest, OrthographicEyeFogIgnoresTheWorldView)
{
    auto pass = TestPass(Screen);
    pass.projection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 1.0f, 100.0f);
    pass.view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
    m_batch.Begin(pass);
    m_batch.SetFog(ModelFog{.color = glm::vec3(0.0f), .start = 10.0f, .end = 30.0f});
    ModelDrawState state{WhiteSlot, ModelBlend::Opaque, false};
    state.space = Client::Render::ModelCoordinateSpace::Eye;
    AddSquare(state, 20.0f, Color{1, 1, 1, 0.5f});
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{128, 128, 128, 128});
}

// The fog color is the command's, and glDisable(GL_FOG) removes it: the green at 40, after the end, comes out red with
// fog and green without.
TEST_F(GlModelRendererTest, TheFogHasItsColorAndTurnsOff)
{
    m_batch.SetFog(ModelFog{.color = glm::vec3(1.0f, 0.0f, 0.0f), .start = 10.0f, .end = 30.0f});
    AddSquare({WhiteSlot, ModelBlend::Opaque, false}, 40.0f, GreenColor);
    DrawBatch();
    ExpectNear(Cell(0), Red);

    m_batch.Begin(TestPass(Screen));
    m_batch.SetFog(std::nullopt);
    AddSquare({WhiteSlot, ModelBlend::Opaque, false}, 40.0f, GreenColor);
    DrawBatch();
    ExpectNear(Cell(0), Green);
}

// GL_ADD (6ac/2, 0x5489eb): 50 % gray plus (0.25; 0.5; 0.75) gives (0.75; 1; 1), clamped, and alpha is the texel's
// times the color's. With GL_MODULATE, the same square gives the product.
TEST_F(GlModelRendererTest, TheAddTextureEnvSumsTheTexelAndTheColor)
{
    const Color color{0.25f, 0.5f, 0.75f, 0.5f};
    m_batch.SetTextureEnv(TextureEnvMode::Add);
    AddSquare({GraySlot, ModelBlend::Opaque, false}, 10.0f, color);
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{192, 255, 255, 128});

    m_batch.Begin(TestPass(Screen));
    m_batch.SetTextureEnv(TextureEnvMode::Modulate);
    AddSquare({GraySlot, ModelBlend::Opaque, false}, 10.0f, color);
    DrawBatch();
    ExpectNear(Cell(0), Rgba8{32, 64, 96, 128});
}

// Q-C29: a slot that never had a texture binds the default texture, so the mesh draws untextured with its vertex
// colors.
TEST_F(GlModelRendererTest, AMissingSlotDrawsUntexturedWithAWarning)
{
    AddSquare({MissingSlot, ModelBlend::Opaque, true}, 10.0f, GreenColor);

    DrawBatch();
    DrawBatch();

    ExpectNear(Cell(0), Green);
    std::size_t warnings = 0;
    for (const std::string& line : m_sink.lines)
    {
        warnings += line.find("0x2a") != std::string::npos ? 1u : 0u;
    }
    EXPECT_EQ(warnings, 1u);
}
} // namespace

TEST_F(GlModelRendererTest, ItemOverlayMultipliesTheDestinationColor)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{0.8f, 0.6f, 0.4f, 1.0f});
    AddSquare({WhiteSlot, ModelBlend::Multiplicative, true}, 5.0f, Color{0.5f, 0.25f, 0.75f, 1.0f});

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{102, 38, 77, 255});
}

TEST_F(GlModelRendererTest, ItemOverlayDoesNotWriteTheDepth)
{
    AddSquare({WhiteSlot, ModelBlend::Multiplicative, true}, 5.0f, GreenColor, true);
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, GreenColor);

    DrawBatch();

    ExpectNear(Cell(0), Green);
}

TEST_F(GlModelRendererTest, ItemOverlayDoesNotCullTheBackFace)
{
    AddSquare({WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{1, 1, 1, 1});
    AddSquare({WhiteSlot, ModelBlend::Multiplicative, true}, 5.0f, Color{0.5f, 0.5f, 0.5f, 1}, true);

    DrawBatch();

    ExpectNear(Cell(0), Rgba8{128, 128, 128, 255});
}
