#include "Render/Gl/GlFrameTarget.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/TextureEnvMode.h"
#include "Render/Frame/SceneCanvas.h"
#include "Render/Frame/SceneText.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Core::PixelSize;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::Color;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::OriginalQuad;
using Client::Render::TextureSlot;
using Client::Render::Frame::RasterizerFactory;
using Client::Render::Frame::SceneCanvas;
using Client::Render::Gl::GlFrameTarget;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Render::Text::FontKind;
using Client::Render::Text::IGlyphRasterizer;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FramebufferSide;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Black{0, 0, 0, 255};
constexpr Rgba8 White{255, 255, 255, 255};
constexpr Rgba8 Red{255, 0, 0, 255};

constexpr TextureSlot WhiteSlot{0x1};
constexpr auto Side = static_cast<float>(FramebufferSide);
constexpr auto Half = Side / 2.0f;

constexpr TextStyle WhiteText{FontKind::Normal, TextColors{0xffffffff, 0x00000000}};

// A pixel quad without flags: [left, right) × [bottom, top) with y up.
OriginalQuad PixelQuad(float left, float bottom, float right, float top)
{
    return OriginalQuad{.slot = WhiteSlot, .x = left, .y = Side - top, .width = right - left, .height = top - bottom};
}

class GlFrameTargetTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        m_textures.emplace(Gl());
        const std::array<LoadedTexture, 1> white{LoadedTexture{WhiteSlot.value,
                                                               {},
                                                               Texture{.width = 1,
                                                                       .height = 1,
                                                                       .textureWidth = 1,
                                                                       .textureHeight = 1,
                                                                       .format = PixelFormat::Rgba,
                                                                       .pixels = {255, 255, 255, 255}}}};
        ASSERT_TRUE(m_textures->Load(white).HasValue());
        auto target = GlFrameTarget::Create(
            Gl(), *m_textures, Fonts(), [this] { return m_screen; }, [this] { ++m_presents; }, m_log);
        ASSERT_TRUE(target.HasValue()) << target.Error();
        m_target = std::move(target).Value();
        ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    void TearDown() override
    {
        // No frame may leave a GL error for what uploads later.
        if (m_context)
        {
            EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
        }
    }

    std::optional<GLint> ReadUniform(const char* name)
    {
        PFNGLGETUNIFORMIVPROC read = nullptr;
        std::string error;
        if (!Client::Platform::Sdl::LoadGlFunction(read, "glGetUniformiv", error))
            return std::nullopt;
        GLint program = 0;
        Gl().getIntegerv(GL_CURRENT_PROGRAM, &program);
        const GLint location = Gl().getUniformLocation(static_cast<GLuint>(program), name);
        if (location < 0)
            return std::nullopt;
        GLint value = -1;
        read(static_cast<GLuint>(program), location, &value);
        return value;
    }

    // The fake rasterizer, with letters an eighth of the width wide, and the widths it was opened with.
    RasterizerFactory Fonts()
    {
        return [this](int screenWidth) -> Client::Core::Expected<std::unique_ptr<IGlyphRasterizer>, std::string>
        {
            m_fontWidths.push_back(screenWidth);
            return std::unique_ptr<IGlyphRasterizer>(std::make_unique<FakeGlyphRasterizer>(screenWidth / 8, 8));
        };
    }

    // Members before m_target die after it: the table outlives the frame and its cache.
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    std::optional<TextureTable> m_textures;
    PixelSize m_screen{FramebufferSide, FramebufferSide};
    int m_presents = 0;
    std::vector<int> m_fontWidths;
    std::unique_ptr<GlFrameTarget> m_target;
};

// GlDrawTest's framebuffer starts transparent: the frame clears it to opaque black.
TEST_F(GlFrameTargetTest, ClearsToBlack)
{
    ASSERT_EQ(Cells(1, 1)[0], Transparent);

    m_target->BeginFrame(std::chrono::milliseconds{40});
    m_target->Present();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>(4, Black)));
}

TEST_F(GlFrameTargetTest, DrawsTheBatchOnPresent)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.sprites.Quad(PixelQuad(0, 0, Side, Side));

    EXPECT_EQ(Cells(1, 1)[0], Black);
    EXPECT_EQ(m_presents, 0);
    m_target->Present();

    EXPECT_EQ(Cells(1, 1)[0], White);
    EXPECT_EQ(m_presents, 1);
}

// The end of the 2D empties the batch but keeps its state: the red color stays, and what comes after goes on top.
TEST_F(GlFrameTargetTest, TheEndOfTheTwoDimensionsDrawsWhatCameBefore)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.sprites.Quad(PixelQuad(0, 0, Side, Side));
    canvas.flushSprites();

    EXPECT_EQ(Cells(1, 1)[0], White);
    EXPECT_TRUE(canvas.sprites.Vertices().empty());
    canvas.sprites.SetColor(Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f});
    canvas.flushSprites();
    canvas.sprites.Quad(PixelQuad(0, 0, Half, Side));
    m_target->Present();

    EXPECT_EQ(Cells(2, 1), (std::vector<std::optional<Rgba8>>{Red, White}));
    EXPECT_EQ(m_presents, 1);
}

TEST_F(GlFrameTargetTest, TheEndOfTheModelsDrawsThePassAndTheTwoDGoesOver)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.models.Begin(Client::Tests::TestPass(m_screen));
    Client::Tests::AddSquare(canvas.models, ModelDrawState{WhiteSlot, ModelBlend::Opaque, true}, 1.1f,
                             Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f});
    canvas.flushModels();

    EXPECT_EQ(Cells(1, 1)[0], Red);
    EXPECT_TRUE(canvas.models.Vertices().empty());
    canvas.sprites.Quad(PixelQuad(0, 0, Half, Side));
    m_target->Present();

    EXPECT_EQ(Cells(2, 1), (std::vector<std::optional<Rgba8>>{White, Red}));
}

TEST_F(GlFrameTargetTest, StateOnlyModelCommandsReachGlWhenTheBatchHasNoVertices)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.models.Begin(Client::Tests::TestPass(m_screen));
    Gl().disable(GL_BLEND);
    Gl().blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    auto state = canvas.models.State();
    state.blend = ModelBlend::InverseColorAdditive;
    canvas.models.ApplyOperation(state, Client::Render::ModelStateOperation::Blend);
    ASSERT_TRUE(canvas.models.Vertices().empty());
    canvas.flushModels();

    GLint value = 0;
    Gl().getIntegerv(GL_BLEND, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_BLEND_SRC_RGB, &value);
    EXPECT_EQ(value, GL_ONE_MINUS_SRC_COLOR);
    Gl().getIntegerv(GL_BLEND_DST_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    m_target->Present();
}

// 548a06/548a4a: the restore after the last draw also reaches the shader.
TEST_F(GlFrameTargetTest, TrailingTextureEnvRestoreReachesGlThroughAnEmptyContinuation)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.models.Begin(Client::Tests::TestPass(m_screen));
    canvas.models.SetTextureEnv(Client::Render::TextureEnvMode::Add);
    Client::Tests::AddSquare(canvas.models, canvas.models.State(), 20, {1, 1, 1, 1});
    canvas.flushModels();
    ASSERT_EQ(ReadUniform("textureAdd"), 1);
    ASSERT_FALSE(canvas.models.InitializesMaterial());
    ASSERT_TRUE(canvas.models.Vertices().empty());
    canvas.models.SetTextureEnv(Client::Render::TextureEnvMode::Modulate);
    const auto serial = canvas.models.StateSerial();
    canvas.models.SetTextureEnv(Client::Render::TextureEnvMode::Modulate);
    EXPECT_EQ(canvas.models.StateSerial(), serial);
    Gl().disable(GL_CULL_FACE);
    Gl().depthMask(GL_FALSE);
    canvas.flushModels();
    EXPECT_EQ(ReadUniform("textureAdd"), 0);
    GLint value = 0;
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);
    m_target->Present();
}

TEST_F(GlFrameTargetTest, TrailingFogChangesReachGlThroughEmptyContinuations)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.models.Begin(Client::Tests::TestPass(m_screen));
    canvas.models.SetTextureEnv(Client::Render::TextureEnvMode::Add);
    Client::Tests::AddSquare(canvas.models, canvas.models.State(), 20, {1, 1, 1, 1});
    canvas.flushModels();
    ASSERT_EQ(ReadUniform("fog"), 0);
    ASSERT_EQ(ReadUniform("textureAdd"), 1);
    ASSERT_FALSE(canvas.models.InitializesMaterial());

    const Client::Render::ModelFog fog{.color = {0.25f, 0.5f, 0.75f}, .start = 10, .end = 30};
    canvas.models.SetFog(fog);
    ASSERT_TRUE(canvas.models.Vertices().empty());
    canvas.flushModels();
    EXPECT_EQ(ReadUniform("fog"), 1);
    EXPECT_EQ(ReadUniform("textureAdd"), 1);

    canvas.models.SetFog(std::nullopt);
    canvas.models.SetFog(std::nullopt);
    Gl().disable(GL_CULL_FACE);
    Gl().depthMask(GL_FALSE);
    canvas.flushModels();
    EXPECT_EQ(ReadUniform("fog"), 0);
    EXPECT_EQ(ReadUniform("textureAdd"), 1);
    GLint value = 0;
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);
    m_target->Present();
}

TEST_F(GlFrameTargetTest, ContinuationKeepsPartialMaterialAndANewWorldPassRestoresItsNativeAxes)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    const auto pass = Client::Tests::TestPass(m_screen);
    canvas.models.Begin(pass);
    Gl().enable(GL_CULL_FACE);
    auto state = canvas.models.State();
    state.cullEnabled = false;
    state.depthWrite = false;
    canvas.models.ApplyOperation(state, Client::Render::ModelStateOperation::Cull);
    canvas.models.ApplyOperation(state, Client::Render::ModelStateOperation::DepthWrite);
    canvas.flushModels();
    GLint value = 0;
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);

    state.blend = ModelBlend::Additive;
    canvas.models.ApplyOperation(state, Client::Render::ModelStateOperation::Blend);
    canvas.flushModels();
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);

    canvas.models.Begin(pass);
    canvas.flushModels();
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_BLEND_SRC_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    m_target->Present();
}

// The additive blend leaves the depth mask off, and the depth glClear honors it: the next frame clears it anyway, and a
// model farther than the previous one is drawn.
TEST_F(GlFrameTargetTest, EachFrameClearsTheDepth)
{
    SceneCanvas& first = m_target->BeginFrame(std::chrono::milliseconds{40});
    first.models.Begin(Client::Tests::TestPass(m_screen));
    Client::Tests::AddSquare(first.models, ModelDrawState{WhiteSlot, ModelBlend::Opaque, true}, 5.0f,
                             Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f});
    Client::Tests::AddSquare(first.models, ModelDrawState{WhiteSlot, ModelBlend::Additive, true}, 5.0f,
                             Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f});
    first.flushModels();
    m_target->Present();

    SceneCanvas& second = m_target->BeginFrame(std::chrono::milliseconds{80});
    second.models.Begin(Client::Tests::TestPass(m_screen));
    Client::Tests::AddSquare(second.models, ModelDrawState{WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{});
    second.flushModels();
    m_target->Present();

    EXPECT_EQ(Cells(1, 1)[0], White);
}

TEST_F(GlFrameTargetTest, TheCanvasHasTheClockAndTheScreen)
{
    const SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{1234});

    EXPECT_EQ(canvas.frameClock, std::chrono::milliseconds{1234});
    EXPECT_EQ(canvas.screen.width, FramebufferSide);
    EXPECT_EQ(canvas.screen.height, FramebufferSide);
    EXPECT_EQ(canvas.sprites.Screen().width, FramebufferSide);
    m_target->Present();
}

// The first letter (the fake rasterizer's 3 × 4 ink at the top left) comes out white.
TEST_F(GlFrameTargetTest, DrawsTheTextFromTheTable)
{
    SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    canvas.text.Draw(0, 0, "a", WhiteText);
    m_target->Present();

    EXPECT_NE(m_textures->Find(Client::Render::FirstTextTextureSlot), nullptr);
    const std::vector<Rgba8> pixels = m_framebuffer->ReadPixels();
    EXPECT_EQ(pixels[Client::Tests::PixelIndex(0, FramebufferSide - 1, FramebufferSide)], White);
    EXPECT_EQ(pixels[Client::Tests::PixelIndex(FramebufferSide - 1, 0, FramebufferSide)], Black);
    EXPECT_TRUE(m_sink.lines.empty());
}

TEST_F(GlFrameTargetTest, TheFontsFollowTheScreenWidth)
{
    const SceneCanvas& first = m_target->BeginFrame(std::chrono::milliseconds{40});
    EXPECT_EQ(first.text.Measure(FontKind::Normal, "ab").width, 2 * (FramebufferSide / 8));
    m_target->Present();
    m_screen = PixelSize{FramebufferSide / 2, FramebufferSide};

    const SceneCanvas& second = m_target->BeginFrame(std::chrono::milliseconds{80});

    EXPECT_EQ(second.text.Measure(FontKind::Normal, "ab").width, 2 * (FramebufferSide / 16));
    EXPECT_EQ(m_fontWidths, (std::vector<int>{FramebufferSide, FramebufferSide / 2}));
    m_target->Present();
}
} // namespace
