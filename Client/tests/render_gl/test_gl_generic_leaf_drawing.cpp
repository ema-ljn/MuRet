#include "Scenes/LeafDrawingServices.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Billboard.h"
#include "Render/Gl/GlFramebuffer.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameModels.h"
#include "Scenes/ILeafDrawingServices.h"
#include "Scenes/LorenciaLeafTextureSet.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/Leaves.h"
#include "support/GlDrawTest.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Result = Client::Render::IParticleDrawBackend::Result;
using Client::Render::ModelBatch;

// Cambia sólo los inputs vivos del caller; todos los servicios se delegan al provider real.
class MixedRoutes final : public Client::Scenes::ILeafDrawingServices
{
public:
    MixedRoutes(Client::Scenes::LeafDrawingServices& real, std::int32_t& map) : m_real(real), m_map(map) {}
    Result Blend(Client::World::LeafBlend blend) override
    {
        return m_real.Blend(blend);
    }
    Result WhiteColor() override
    {
        return m_real.WhiteColor();
    }
    Result BindTexture(std::int32_t selector) override
    {
        const auto result = m_real.BindTexture(selector);
        if (result.HasValue())
        {
            ++m_bindings;
            m_map = m_bindings == 2 ? 2 : 0;
        }
        return result;
    }
    Result PushMatrix() override
    {
        return m_real.PushMatrix();
    }
    Result Translate(float x, float y, float z) override
    {
        return m_real.Translate(x, y, z);
    }
    Result Quad(ModelBatch& batch, const Client::World::LeafQuad& quad) override
    {
        return m_real.Quad(batch, quad);
    }
    Result Billboard(ModelBatch& batch, const Client::Scenes::LeafBillboardRequest& request) override
    {
        return m_real.Billboard(batch, request);
    }
    Result PopMatrix() override
    {
        return m_real.PopMatrix();
    }

private:
    Client::Scenes::LeafDrawingServices& m_real;
    std::int32_t& m_map;
    int m_bindings = 0;
};

class GlGenericLeafDrawingTest : public Client::Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        auto created = Client::Render::Gl::GlModelRenderer::Create(Gl());
        ASSERT_TRUE(created.HasValue());
        m_modelsRenderer.emplace(std::move(created).Value());
        m_textures.emplace(Gl());
        const auto report = m_models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::LorenciaLeafTextureSet,
                                                  *m_textures, m_log);
        ASSERT_EQ(report.uploaded, 1u);
        ASSERT_EQ(report.skipped, 2u);
        const auto* driver = Gl().getString(GL_RENDERER);
        ASSERT_NE(driver, nullptr);
        RecordProperty("glRenderer", std::string(reinterpret_cast<const char*>(driver)));
    }
    Client::Render::Pass3D Pass() const
    {
        Client::Render::Pass3D pass;
        pass.viewport = {0, 0, Client::Tests::FramebufferSide, Client::Tests::FramebufferSide};
        // eye=(world.x+0.5,world.z,-world.y): rotación y traslación exactas, no identidad.
        pass.view = glm::mat4{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0.5f, 0, 0, 1};
        return pass;
    }
    std::optional<Client::Render::Gl::GlModelRenderer> m_modelsRenderer;
    std::optional<Client::Render::Gl::TextureTable> m_textures;
    Client::Scenes::GameModels m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

TEST_F(GlGenericLeafDrawingTest, RealWorldEyeWorldMatchesIndependentAnalyticVerticesAndPersistentColor)
{
    using namespace Client::Render;
    auto pass = Pass();
    pass.projection[0][0] = 1.0f / 16.0f;
    pass.projection[1][1] = 1.0f / 8.0f;
    pass.projection[2][2] = 1.0f / 16.0f;
    ModelBatch batch;
    batch.Begin(pass);
    batch.SetState({std::nullopt, ModelBlend::Opaque, false});
    const auto view = ViewRows(pass.view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(m_models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(m_models), std::ref(backend), std::cref(view)};
    Client::World::Leaves leaves;
    for (std::size_t index : {std::size_t{0}, std::size_t{80}, std::size_t{199}})
    {
        leaves.All()[index].alive = 0x80;
        leaves.All()[index].type = 100;
        leaves.All()[index].scale = 6;
    }
    leaves.All()[0].position = {-8.5f, 0, 0};
    leaves.All()[80].position = {-0.5f, 0, 0};
    leaves.All()[80].light = {0.5f, 0.75f, 0.25f};
    leaves.All()[199].position = {7.5f, 0, 0};
    std::int32_t map = 0;
    constexpr std::int32_t Selector = 0;
    MixedRoutes routes{provider, map};
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(batch, leaves, map, Selector, &routes).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 18u);
    std::vector<ModelCoordinateSpace> spaces;
    for (const auto& command : batch.Commands())
        if (command.vertexCount != 0)
            spaces.push_back(command.state.space);
    EXPECT_EQ(spaces,
              (std::vector{ModelCoordinateSpace::World, ModelCoordinateSpace::Eye, ModelCoordinateSpace::World}));
    EXPECT_FALSE(backend.HasPushedMatrix());
    EXPECT_EQ(batch.Vertices()[12].color, (Color{0.5f, 0.75f, 0.25f, 0.5f}));
    m_modelsRenderer->Draw(batch, *m_textures, m_log);
    ASSERT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
    const auto actual = m_framebuffer->ReadPixels();
    bool anyColor = false;
    for (const auto& pixel : actual)
        anyColor = anyColor || pixel.alpha != 0;
    ASSERT_TRUE(anyColor); // Una comparación de dos imágenes vacías nunca es PASS.

    Gl().depthMask(GL_TRUE);
    Gl().clearColor(0, 0, 0, 0);
    Gl().clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ModelBatch expected;
    expected.Begin(pass);
    constexpr std::array<glm::vec2, 4> Uv{glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0), glm::vec2(0, 0)};
    const auto append = [&](const std::array<glm::vec3, 4>& points, ModelCoordinateSpace space, Color color)
    {
        expected.SetState({TextureSlot{100}, ModelBlend::Alpha, false, space});
        const auto v = [&](std::size_t index) { return ModelVertex{points[index], Uv[index], color}; };
        expected.Triangle({v(0), v(1), v(2)});
        expected.Triangle({v(0), v(2), v(3)});
    };
    // Literales analíticos de5306a0/621320 y621400; no calculados con LeafQuadOf/MakeBillboard.
    append({glm::vec3{-11.5f, -3, -3}, {-5.5f, 3, -3}, {-5.5f, 3, 3}, {-11.5f, -3, 3}}, ModelCoordinateSpace::World,
           {1, 1, 1, 1});
    append({glm::vec3{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}}, ModelCoordinateSpace::Eye,
           {0.5f, 0.75f, 0.25f, 0.5f});
    append({glm::vec3{4.5f, -3, -3}, {10.5f, 3, -3}, {10.5f, 3, 3}, {4.5f, -3, 3}}, ModelCoordinateSpace::World,
           {0.5f, 0.75f, 0.25f, 0.5f});
    m_modelsRenderer->Draw(expected, *m_textures, m_log);
    EXPECT_EQ(actual, m_framebuffer->ReadPixels());
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST_F(GlGenericLeafDrawingTest, AGenericQuadUsesTheInheritedUntexturedRasterAndRgbaWithoutForcingWhite)
{
    using namespace Client::Render;
    ModelBatch batch;
    batch.Begin(Pass());
    batch.SetState({std::nullopt, ModelBlend::Opaque, false});
    const auto view = ViewRows(batch.Pass().view);
    ModelBatchParticleBackend backend{std::ref(batch), std::cref(m_models), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(m_models), std::ref(backend), std::cref(view)};
    ASSERT_TRUE(backend.Color4({0.25f, 0.5f, 0.75f, 0.5f}).HasValue());
    ASSERT_TRUE(provider.PushMatrix().HasValue());
    ASSERT_TRUE(provider.Translate(-0.5f, 0, 0).HasValue());
    constexpr Models::OriginalMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const auto quad = Client::World::LeafQuadOf({-0.5f, 0, 0}, Identity, {1, 1});
    ASSERT_TRUE(provider.Quad(batch, quad).HasValue());
    ASSERT_TRUE(provider.PopMatrix().HasValue());
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state.space, ModelCoordinateSpace::World);
    EXPECT_EQ(batch.Commands()[0].state.blend, ModelBlend::Opaque);
    EXPECT_FALSE(batch.Commands()[0].state.texture.has_value());
    m_modelsRenderer->Draw(batch, *m_textures, m_log);
    const auto pixels = m_framebuffer->ReadPixels();
    const auto pixel = pixels[Client::Tests::PixelIndex(32, 32, Client::Tests::FramebufferSide)];
    EXPECT_NEAR(pixel.red, 64, 1);
    EXPECT_NEAR(pixel.green, 128, 1);
    EXPECT_NEAR(pixel.blue, 191, 1);
    EXPECT_NEAR(pixel.alpha, 128, 1);
    EXPECT_EQ(backend.State().color, (Color{0.25f, 0.5f, 0.75f, 0.5f}));
    EXPECT_EQ(backend.State().uv, glm::vec2(0));
    EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
}
} // namespace
