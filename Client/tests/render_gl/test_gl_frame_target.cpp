#include "Render/Gl/GlFrameTarget.h"
#include "Render/ModelBatch.h"
#include "Render/Billboard.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/LoadedWorld.h"
#include "Scenes/DaviasLeafTextureSet.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameCamera.h"
#include "World/PortableLeafWaveEvaluator.h"
#include "World/PortableParticleTrigonometry.h"
#include "support/GoldenDir.h"
#include "Input/InputState.h"
#include "Scenes/LeafDrawingServices.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/Leaves.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneText.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/GlDrawTest.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::Render::Gl::GlFrameTarget: el frame de una escena en GL (CL5.4), en el framebuffer de
// 64 × 64 de GlDrawTest. Limpia en negro, dibuja lo del lote en el fin del 2D y en el Present, en
// orden, presenta una vez por frame, el texto sale de la tabla de texturas y GL no queda con un
// error.
namespace
{
using Client::App::PixelSize;
using Client::Data::Textures::LoadedTexture;
using Client::Data::Textures::PixelFormat;
using Client::Data::Textures::Texture;
using Client::Render::Color;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::OriginalQuad;
using Client::Render::TextureSlot;
using Client::Render::Gl::GlFrameTarget;
using Client::Render::Gl::Rgba8;
using Client::Render::Gl::TextureTable;
using Client::Render::Text::FontKind;
using Client::Render::Text::IGlyphRasterizer;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Scenes::RasterizerFactory;
using Client::Scenes::SceneCanvas;
using Client::Tests::FakeGlyphRasterizer;
using Client::Tests::FramebufferSide;

constexpr Rgba8 Transparent{0, 0, 0, 0};
constexpr Rgba8 Black{0, 0, 0, 255};
constexpr Rgba8 White{255, 255, 255, 255};
constexpr Rgba8 Red{255, 0, 0, 255};

constexpr TextureSlot WhiteSlot{0x1};
constexpr auto Side = static_cast<float>(FramebufferSide);
constexpr auto Half = Side / 2.0f;

// El texto en blanco sobre un fondo transparente.
constexpr TextStyle WhiteText{FontKind::Normal, TextColors{0xffffffff, 0x00000000}};

// Un quad en píxeles, sin banderas: [left, right) × [bottom, top) con la y para arriba.
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
        // Ningún frame deja un error de GL para lo que sube después.
        if (m_context)
        {
            EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
        }
    }

    // El rasterizador falso, con la letra de un octavo del ancho, y los anchos con que se abrió.
    RasterizerFactory Fonts()
    {
        return [this](int screenWidth) -> Client::Core::Expected<std::unique_ptr<IGlyphRasterizer>, std::string>
        {
            m_fontWidths.push_back(screenWidth);
            return std::unique_ptr<IGlyphRasterizer>(std::make_unique<FakeGlyphRasterizer>(screenWidth / 8, 8));
        };
    }

    // Lo de antes de m_target muere después: la tabla vive más que el frame y su caché.
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    std::optional<TextureTable> m_textures;
    PixelSize m_screen{FramebufferSide, FramebufferSide};
    int m_presents = 0;
    std::vector<int> m_fontWidths;
    std::unique_ptr<GlFrameTarget> m_target;
};

// El framebuffer de GlDrawTest empieza transparente: el frame lo limpia en negro opaco.
TEST_F(GlFrameTargetTest, ClearsToBlack)
{
    ASSERT_EQ(Cells(1, 1)[0], Transparent);

    m_target->BeginFrame(std::chrono::milliseconds{40});
    m_target->Present();

    EXPECT_EQ(Cells(2, 2), (std::vector<std::optional<Rgba8>>(4, Black)));
}

// Lo del lote sale en el Present, no antes, y el Present presenta una vez.
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

// El fin del 2D dibuja lo de antes y vacía el lote con su estado: el color rojo sigue, y lo de
// después va encima.
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

// El fin del pase 3D dibuja los modelos con su profundidad, y el 2D que sigue va encima (CL6.3).
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

// La mezcla aditiva deja la máscara de la profundidad apagada, y el glClear de la profundidad la
// respeta: el frame siguiente la limpia igual, y un modelo más lejos que el de antes se dibuja.
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

    SceneCanvas& second = m_target->BeginFrame(std::chrono::milliseconds{40});
    second.models.Begin(Client::Tests::TestPass(m_screen));
    Client::Tests::AddSquare(second.models, ModelDrawState{WhiteSlot, ModelBlend::Opaque, true}, 20.0f, Color{});
    second.flushModels();
    m_target->Present();

    EXPECT_EQ(Cells(1, 1)[0], White);
}

// El lienzo trae el reloj y la pantalla del frame.
TEST_F(GlFrameTargetTest, TheCanvasHasTheClockAndTheScreen)
{
    const SceneCanvas& canvas = m_target->BeginFrame(std::chrono::milliseconds{1234});

    EXPECT_EQ(canvas.frameClock, std::chrono::milliseconds{1234});
    EXPECT_EQ(canvas.screen.width, FramebufferSide);
    EXPECT_EQ(canvas.screen.height, FramebufferSide);
    EXPECT_EQ(canvas.sprites.Screen().width, FramebufferSide);
    m_target->Present();
}

// El texto sube su caja a la tabla de las escenas y sale con el lote: la primera letra (la tinta
// de 3 × 4 arriba a la izquierda del rasterizador falso) queda en blanco.
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

// Las fuentes son las del ancho de cada frame: con otra pantalla, otras.
TEST_F(GlFrameTargetTest, TheFontsFollowTheScreenWidth)
{
    const SceneCanvas& first = m_target->BeginFrame(std::chrono::milliseconds{40});
    EXPECT_EQ(first.text.Measure(FontKind::Normal, "ab").width, 2 * (FramebufferSide / 8));
    m_target->Present();
    m_screen = PixelSize{FramebufferSide / 2, FramebufferSide};

    const SceneCanvas& second = m_target->BeginFrame(std::chrono::milliseconds{40});

    EXPECT_EQ(second.text.Measure(FontKind::Normal, "ab").width, 2 * (FramebufferSide / 16));
    EXPECT_EQ(m_fontWidths, (std::vector<int>{FramebufferSide, FramebufferSide / 2}));
    m_target->Present();
}
// Mata el consumidor que descarta comandos de estado por no tener vértices.
TEST_F(GlFrameTargetTest, FlushModelsAppliesAnEmptyLeafPoolMaterialWithoutInventingGeometry)
{
    auto& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    Gl().disable(GL_BLEND);
    Gl().enable(GL_CULL_FACE);
    Gl().depthMask(GL_TRUE);
    Client::Scenes::GameModels models;
    const auto view = Client::Render::ViewRows(canvas.models.Pass().view);
    Client::Render::ModelBatchParticleBackend backend{std::ref(canvas.models), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    const std::int32_t map = 2;
    const std::int32_t selector = 0;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(canvas.models, leaves, map, selector, &provider).HasValue());
    ASSERT_TRUE(canvas.models.Vertices().empty());
    ASSERT_FALSE(canvas.models.Commands().empty());
    canvas.flushModels();
    GLint value = -1;
    Gl().getIntegerv(GL_BLEND, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_BLEND_SRC_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    Gl().getIntegerv(GL_BLEND_DST_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    EXPECT_TRUE(canvas.models.Vertices().empty());
    EXPECT_TRUE(canvas.models.Commands().empty());
    EXPECT_EQ(Cells(1, 1)[0], Black);
    m_target->Present();
}

TEST_F(GlFrameTargetTest, PresentAppliesAStateOnlyBindingThroughTheRealConsumer)
{
    auto& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    Gl().bindTexture(GL_TEXTURE_2D, 0);
    canvas.models.ApplyState(ModelDrawState{.texture = WhiteSlot, .blend = ModelBlend::Additive});
    ASSERT_TRUE(canvas.models.Vertices().empty());
    m_target->Present();
    GLint actual = 0;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &actual);
    EXPECT_NE(actual, 0);
    EXPECT_EQ(m_presents, 1);
    EXPECT_EQ(Cells(1, 1)[0], Black);
}
TEST_F(GlFrameTargetTest, GameSceneDaviasUsesTheDurableTargetAndPublishedViewAcrossRealFrames)
{
    Client::Scenes::WorldState world(*m_textures);
    world.Runtime().map = 2;
    world.Runtime().drawingEnabled = true;
    world.Runtime().worldTime = 40.0f;
    world.Actors()[0].dying = true;
    world.Actors()[0].position = {13350, 12750, 300};
    std::vector<std::string> skipped;
    ASSERT_TRUE(world.Resources()->terrain.Load(Client::Tests::RepoDataDir(), 2, skipped).HasValue());
    Client::Scenes::GameModels models;
    ASSERT_EQ(
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
            .uploaded,
        3u);
    Client::Render::Models::ClothWorld cloth;
    cloth.openCounterAccesses = 1;
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::World::PortableParticleTrigonometry particleMath;
    Client::Render::Models::OriginalMatrix view{};
    auto& batch = m_target->Models();
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    const Client::Scenes::SceneContext context{.dataRoot = Client::Tests::RepoDataDir(),
                                               .textures = *m_textures,
                                               .log = m_log,
                                               .cloth = &cloth,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .world = &world,
                                               .leafWaves = &evaluator,
                                               .worldTextureModels = &models,
                                               .modelBatch = &batch,
                                               .leafView = &view,
                                               .leafDrawing = &provider,
                                               .particleTrigonometry = &particleMath};
    Client::Scenes::GameScene scene(context, nullptr, &models);
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds{40}), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    m_target->Present();
    const auto withoutLeaves = Cells(FramebufferSide, FramebufferSide);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    // Entrada sintética grande para observar píxeles inequívocos, además de los80 slots C5 reales.
    world.Resources()->leaves.All()[0].position = world.Actors()[0].position;
    world.Resources()->leaves.All()[0].scale = 100;
    scene.PrepareFrame();
    auto& canvas = m_target->BeginFrame(std::chrono::milliseconds{40});
    std::size_t eyeVertices = 0;
    const auto realFlush = canvas.flushModels;
    canvas.flushModels = [&]
    {
        for (const auto& command : canvas.models.Commands())
            if (command.state.space == Client::Render::ModelCoordinateSpace::Eye && command.state.texture &&
                (command.state.texture->value == 100 || command.state.texture->value == 101))
                eyeVertices += command.vertexCount;
        realFlush();
    };
    scene.Render(canvas, 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(view, Client::Render::ViewRows(batch.Pass().view));
    m_target->Present();
    EXPECT_EQ(eyeVertices, 480u); // 80 slots, dos triángulos; no doble aplicación de la vista en el renderer.
    EXPECT_NE(Cells(FramebufferSide, FramebufferSide), withoutLeaves);
    EXPECT_EQ(m_presents, 2);
}
} // namespace
