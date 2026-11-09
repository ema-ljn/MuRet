#include "Scenes/LeafDrawingServices.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Billboard.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/DaviasLeafTextureSet.h"
#include "Scenes/GameModels.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/Leaves.h"
#include "support/GlDrawTest.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
class GlLeafDrawingTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        auto created = Client::Render::Gl::GlModelRenderer::Create(Gl());
        ASSERT_TRUE(created.HasValue());
        m_leafRenderer.emplace(std::move(created).Value());
        m_textures.emplace(Gl());
        const auto* driver = Gl().getString(GL_RENDERER);
        ASSERT_NE(driver, nullptr);
        RecordProperty("glRenderer", std::string(reinterpret_cast<const char*>(driver)));
    }
    std::optional<Client::Render::Gl::GlModelRenderer> m_leafRenderer;
    std::optional<Client::Render::Gl::TextureTable> m_textures;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

// Referencia analítica independiente: World con view identidad frente a621400 con view trasladada.
TEST_F(GlLeafDrawingTest, ActualDaviasProviderMatchesOneViewTransformWithExistingAssets)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
                  .uploaded,
              3u);
    Pass3D pass;
    pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
    pass.view[3][0] = 0.5f;
    pass.view[3][1] = 0.25f;
    ModelBatch batch;
    batch.Begin(pass);
    const auto view = ViewRows(pass.view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices services{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    auto& leaf = leaves.All()[199];
    leaf.alive = 1;
    leaf.type = 100;
    leaf.scale = 1;
    leaf.position = {-0.5f, -0.25f, 0};
    leaf.light = {1, 1, 1};
    constexpr std::int32_t Map = 2;
    constexpr std::int32_t Selector = 0;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &services).HasValue());
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    const auto actual = m_framebuffer->ReadPixels();

    Gl().clearColor(0, 0, 0, 0);
    Gl().clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pass.view = glm::mat4{1};
    ModelBatch expected;
    expected.Begin(pass);
    expected.SetState(ModelDrawState{TextureSlot{100}, ModelBlend::Additive, true});
    const ModelVertex a{{-0.5f, -0.5f, 0}, {0, 1}, {1, 1, 1, 1}};
    const ModelVertex b{{0.5f, -0.5f, 0}, {1, 1}, {1, 1, 1, 1}};
    const ModelVertex c{{0.5f, 0.5f, 0}, {1, 0}, {1, 1, 1, 1}};
    const ModelVertex d{{-0.5f, 0.5f, 0}, {0, 0}, {1, 1, 1, 1}};
    expected.Triangle({a, b, c});
    expected.Triangle({a, c, d});
    m_leafRenderer->Draw(expected, *m_textures, m_log);
    EXPECT_EQ(actual, m_framebuffer->ReadPixels());
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, WorldEyeWorldCommandsSelectTheirOwnMatrixAndKeepDrawOrder)
{
    using namespace Client::Render;
    Pass3D pass;
    pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
    pass.view[3][0] = 0.5f;
    ModelBatch batch;
    batch.Begin(pass);
    ModelDrawState state;
    state.depthTest = false;
    const auto square = [&](float x, const Color& color)
    {
        const ModelVertex a{{x, -0.5f, 0}, {}, color};
        const ModelVertex b{{x + 0.5f, -0.5f, 0}, {}, color};
        const ModelVertex c{{x + 0.5f, 0.5f, 0}, {}, color};
        const ModelVertex d{{x, 0.5f, 0}, {}, color};
        batch.SetState(state);
        batch.Triangle({a, b, c});
        batch.Triangle({a, c, d});
    };
    square(-1.25f, {1, 0, 0, 1});
    state.space = ModelCoordinateSpace::Eye;
    square(-0.25f, {0, 1, 0, 1});
    state.space = ModelCoordinateSpace::World;
    square(-0.25f, {0, 0, 1, 1});
    ASSERT_EQ(batch.Commands().size(), 3u);
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    const auto pixels = m_framebuffer->ReadPixels();
    constexpr int Side = Client::Tests::FramebufferSide;
    EXPECT_EQ(pixels[Client::Tests::PixelIndex(16, 32, Side)], (Gl::Rgba8{255, 0, 0, 255}));
    EXPECT_EQ(pixels[Client::Tests::PixelIndex(32, 32, Side)], (Gl::Rgba8{0, 255, 0, 255}));
    EXPECT_EQ(pixels[Client::Tests::PixelIndex(48, 32, Side)], (Gl::Rgba8{0, 0, 255, 255}));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, AnEmptyPoolStillReachesTheActualGlMaterialState)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    const auto view = ViewRows(batch.Pass().view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    constexpr std::int32_t Map = 2;
    constexpr std::int32_t Selector = 0;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    GLint value = -1;
    Gl().getIntegerv(GL_BLEND, &value);
    EXPECT_EQ(value, GL_TRUE);
    Gl().getIntegerv(GL_BLEND_SRC_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    Gl().getIntegerv(GL_BLEND_DST_RGB, &value);
    EXPECT_EQ(value, GL_ONE);
    Gl().getIntegerv(GL_CULL_FACE, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_WRITEMASK, &value);
    EXPECT_EQ(value, GL_FALSE);
    Gl().getIntegerv(GL_DEPTH_TEST, &value);
    EXPECT_EQ(value, GL_TRUE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, ANegativeSelectorKeepsItsActualPhysicalBindingAfterLateMetadataFailure)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
                  .uploaded,
              3u);
    const auto* texture = m_textures->Find(TextureSlot{100});
    ASSERT_NE(texture, nullptr);
    const std::uint32_t name = texture->Name();
    ASSERT_GT(name, 0u);
    ASSERT_LE(name, static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));
    ModelBatch batch;
    batch.Begin(Client::Tests::TestPass({64, 64}));
    const auto view = ViewRows(batch.Pass().view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    auto& leaf = leaves.All()[199];
    leaf.alive = 1;
    leaf.type = -static_cast<std::int32_t>(name);
    leaf.scale = 8;
    constexpr std::int32_t Map = 2;
    constexpr std::int32_t Selector = 0;
    const auto drawn = Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_EQ(drawn.Error(), std::errc::no_such_file_or_directory);
    EXPECT_TRUE(backend.IsDrawing());
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_EQ(backend.State().rawTextureName, name);
    EXPECT_FALSE(backend.State().texture.has_value());
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    GLint binding = 0;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_EQ(static_cast<std::uint32_t>(binding), name);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, APhysicalNameZeroBindsZeroAndAmbiguousCommandsAreExplicitlyRejected)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
                  .uploaded,
              3u);
    GLint uploaded = 0;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &uploaded);
    ASSERT_NE(uploaded, 0);
    ModelBatch batch;
    Pass3D pass;
    pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
    batch.Begin(pass);
    ModelDrawState state;
    state.rawTextureName = std::uint32_t{0};
    batch.ApplyState(state);
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    GLint binding = -1;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_EQ(binding, 0);

    batch.Begin(pass);
    state.texture = TextureSlot{100};
    batch.ApplyState(state); // Slot + raw0 incumplen el invariante, no tienen prioridad.
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    ASSERT_FALSE(m_sink.levels.empty());
    EXPECT_EQ(m_sink.levels.back(), Client::Core::LogLevel::Error);
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_EQ(binding, 0);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, DurableProviderResynchronizesTwoRealPassesAfterForeignMaterial)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
                  .uploaded,
              3u);
    Pass3D pass;
    pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
    pass.view[3][0] = 0.25f;
    ModelBatch batch;
    batch.Begin(pass);
    auto view = ViewRows(pass.view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    auto& leaf = leaves.All()[199];
    leaf.alive = 1;
    leaf.type = 100;
    leaf.scale = 1;
    leaf.position = {-0.25f, 0, 0};
    leaf.light = {1, 1, 1};
    constexpr std::int32_t Map = 2;
    constexpr std::int32_t Selector = 0;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    const auto first = m_framebuffer->ReadPixels();

    Gl().depthMask(GL_TRUE);
    Gl().clearColor(0, 0, 0, 0);
    Gl().clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pass.view[3][0] = -0.5f;
    batch.Begin(pass);
    batch.ApplyState({TextureSlot{101}, ModelBlend::Alpha, false});
    view = ViewRows(batch.Pass().view); // Mismo objeto durable, contenido del pase real.
    leaf.position.x = 0.5f;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 6u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(-0.5f, -0.5f, 0));
    EXPECT_EQ(batch.Commands().back().state.texture, TextureSlot{100});
    EXPECT_FALSE(batch.Commands().back().state.depthTest);
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    EXPECT_EQ(first, m_framebuffer->ReadPixels());
    GLint depthTest = -1;
    Gl().getIntegerv(GL_DEPTH_TEST, &depthTest);
    EXPECT_EQ(depthTest, GL_FALSE);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlLeafDrawingTest, NotifiedRealReloadRebindsTheNewObjectAndFailedLoadKeepsItsPublishedRow)
{
    using namespace Client::Render;
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, *m_textures, m_log)
                  .uploaded,
              3u);
    const auto* firstTexture = m_textures->Find(TextureSlot{100});
    ASSERT_NE(firstTexture, nullptr);
    const auto firstName = firstTexture->Name();
    Pass3D pass;
    pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
    ModelBatch batch;
    batch.Begin(pass);
    const auto view = ViewRows(pass.view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    auto& leaf = leaves.All()[199];
    leaf.alive = 1;
    leaf.type = 100;
    leaf.scale = 1;
    leaf.light = {0.25f, 0.5f, 0.75f};
    constexpr std::int32_t Map = 2;
    constexpr std::int32_t Selector = 0;
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    EXPECT_FALSE(batch.InvalidateTextureKnowledge().HasValue()); // No se ejecuta la carga prohibida.
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    batch.Begin(pass);
    ASSERT_TRUE(batch.InvalidateTextureKnowledge().HasValue());
    const auto onlyOzt = std::span(Client::Scenes::DaviasLeafTextureSet).first(1);
    ASSERT_EQ(models.LoadTextures(Client::Tests::GoldenDir(), onlyOzt, *m_textures, m_log).uploaded, 1u);
    const auto* replaced = m_textures->Find(TextureSlot{100});
    ASSERT_NE(replaced, nullptr);
    const auto secondName = replaced->Name();
    EXPECT_NE(secondName, firstName); // El nombre nuevo se generó con el previo todavía vivo.
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    EXPECT_EQ(batch.Vertices()[0].color.alpha, 0.25f);
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    GLint binding = 0;
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_EQ(static_cast<GLuint>(binding), secondName);

    batch.Begin(pass);
    ASSERT_TRUE(batch.InvalidateTextureKnowledge().HasValue());
    const std::filesystem::path missing = Client::Tests::GoldenDir() / "missing-world-leaf-root";
    const auto failed = models.LoadTextures(missing, onlyOzt, *m_textures, m_log);
    EXPECT_EQ(failed.uploaded, 0u);
    EXPECT_EQ(failed.skipped, 1u);
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, Map, Selector, &provider).HasValue());
    EXPECT_EQ(batch.Vertices()[0].color.alpha, 0.25f);
    m_leafRenderer->Draw(batch, *m_textures, m_log);
    Gl().getIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_EQ(static_cast<GLuint>(binding), secondName);
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}
} // namespace
