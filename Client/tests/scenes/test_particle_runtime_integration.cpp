#include "Scenes/GameScene.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlFrameTarget.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/DaviasLeafTextureSet.h"
#include "Scenes/GameModels.h"
#include "Scenes/IScene.h"
#include "Scenes/LeafDrawingServices.h"
#include "Scenes/LoadedWorld.h"
#include "Scenes/ParticleDrawServices.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/TextureSets.h"
#include "World/GameCamera.h"
#include "World/Leaves.h"
#include "World/IParticleTrigonometry.h"
#include "World/PortableLeafWaveEvaluator.h"
#include "support/FakeClock.h"
#include "support/FakeGlyphRasterizer.h"
#include "support/GlDrawTest.h"
#include "support/GoldenDir.h"
#include <format>
#include <system_error>
#include <glm/ext/vector_float2.hpp>
#include <bit>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/ParticleDrawing.h"
#include "World/PortableParticleTrigonometry.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace
{
using namespace Client;
class EmptyParticleLibrary final : public Scenes::IModelLibrary
{
public:
    const Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};
class ParticleRuntimeIntegrationTest : public testing::Test
{
protected:
    ParticleRuntimeIntegrationTest()
    {
        m_world.Runtime().drawingEnabled = true;
        m_world.Runtime().map = 8; // No ejecuta familias Leaf ajenas a este fixture.
        m_world.Runtime().worldTime = 0.0f;
        m_world.Actors()[0].dying = true;
    }
    Scenes::SceneContext Context()
    {
        return {.dataRoot = {},
                .textures = m_textures,
                .log = m_log,
                .cloth = &m_cloth,
                .world = &m_world,
                .particleTrigonometry = &m_math};
    }
    Tests::FakeTextureStore m_textures;
    Tests::RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Debug};
    Scenes::WorldState m_world{m_textures};
    Render::Models::ClothWorld m_cloth;
    EmptyParticleLibrary m_models;
    World::PortableParticleTrigonometry m_math;
};
TEST_F(ParticleRuntimeIntegrationTest, SceneStepsOrbitInTheCanonicalPoolAndExpiresBeforeTheMathTail)
{
    auto slots = m_world.Resources()->particles.All();
    slots[0] = {.alive = true,
                .type = 0x60f,
                .subtype = 3,
                .scale = 2,
                .position = {9, 8, 7},
                .light = {1, 2, 3},
                .life = 2,
                .verticalSpeed = 4,
                .velocity = {-1, 0, 0},
                .start = {10, 20, 30}};
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.Step(Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(slots[0].life, 1);
    EXPECT_EQ(slots[0].position, (glm::vec3{10, -20, 11})); // sin(0)*40, 20-cos(0)*40, Z+4.
    scene.Step(Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_FALSE(slots[0].alive);
    EXPECT_EQ(slots[0].life, 0);
    EXPECT_EQ(slots[0].position.z, 15); // El tail aún corre una vez al expirar.
}
TEST_F(ParticleRuntimeIntegrationTest, MissingPublishedTimeFailsBeforeParticleWindAndSlots)
{
    m_world.Runtime().worldTime.reset();
    auto& particle = m_world.Resources()->particles.All()[0];
    particle = {.alive = true, .type = 0x607, .life = 5};
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.Step(Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("partículas: falta T canónico"), std::string::npos);
    EXPECT_EQ(particle.life, 5);
    EXPECT_EQ(m_world.Resources()->particles.Wind().wind, glm::vec2(0));
}
TEST_F(ParticleRuntimeIntegrationTest, UnsupportedSlotKeepsItsPrefixAndDoesNotVisitItsSuccessor)
{
    auto slots = m_world.Resources()->particles.All();
    slots[0] = {
        .alive = true, .type = 0x7fffffff, .position = {1, 2, 3}, .life = 1, .moves = true, .velocity = {4, 5, 6}};
    slots[1] = {.alive = true, .type = 0x60f, .subtype = 3, .life = 4};
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.Step(Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("partículas: Step"), std::string::npos);
    EXPECT_FALSE(slots[0].alive);
    EXPECT_EQ(slots[0].position, (glm::vec3{5, 7, 9}));
    EXPECT_EQ(slots[1].life, 4);
}

// El observador registra argumentos sin mutar mundo/RNG/reloj; delega toda matemática al backend real.
class ObservedParticleMath final : public World::IParticleTrigonometry
{
public:
    Core::Expected<double, World::ParticleError> SineProductPC53(double argument, float multiplier) const override
    {
        arguments.push_back(argument);
        return real.SineProductPC53(argument, multiplier);
    }
    Core::Expected<double, World::ParticleError> CosineProductPC53(double argument, float multiplier) const override
    {
        return real.CosineProductPC53(argument, multiplier);
    }
    mutable std::vector<double> arguments;
    World::PortableParticleTrigonometry real;
};
TEST_F(ParticleRuntimeIntegrationTest, EachTickReadsPublishedTimeAndConsumesTheCanonicalRandom)
{
    ObservedParticleMath math;
    auto context = Context();
    context.particleTrigonometry = &math;
    m_cloth.openCounterAccesses = 1; // Excluye los cierres CRT de tela; quedan dos draws wind + cinco walks.
    auto& p = m_world.Resources()->particles.All()[17];
    p = {.alive = true, .type = 0x5f2, .subtype = 6, .life = 1, .rotation = 13, .verticalSpeed = 7};
    m_world.Runtime().worldTime = 1000.0f;
    Scenes::GameScene scene(context, nullptr, &m_models);
    scene.Step(Input::InputState{});
    ASSERT_FALSE(scene.Failure());
    ASSERT_EQ(math.arguments.size(), 2u);
    EXPECT_EQ(math.arguments[0], 1007.0 * static_cast<double>(std::bit_cast<float>(0x3951b717u)));
    EXPECT_FALSE(p.alive); // La reescritura de life=10 no revive el byte expirado.
    EXPECT_EQ(p.life, 10);
    Render::Models::CrtRandom expected;
    for (int i = 0; i < 7; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(m_cloth.random.State(), expected.State());
    p.alive = true;
    m_world.Runtime().worldTime = 2000.0f;
    scene.Step(Input::InputState{});
    ASSERT_FALSE(scene.Failure());
    ASSERT_EQ(math.arguments.size(), 4u);
    EXPECT_EQ(math.arguments[2], 2007.0 * static_cast<double>(std::bit_cast<float>(0x3951b717u)));
    EXPECT_EQ(p.life, 10);
}
TEST_F(ParticleRuntimeIntegrationTest, FailureIsTerminalAndDoesNotRepeatTheParticleOrClothPrefix)
{
    auto& p = m_world.Resources()->particles.All()[0];
    p = {.alive = true, .type = 0x7fffffff, .life = 4};
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.Step(Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    const auto state = m_cloth.random.State();
    EXPECT_EQ(p.life, 3);
    scene.Step(Input::InputState{});
    EXPECT_EQ(p.life, 3);
    EXPECT_EQ(m_cloth.random.State(), state);
    EXPECT_EQ(m_cloth.logicSteps, 1);
}

// Un único backend productivo para Leaf y Particle; el hook sólo observa/muta aliases en el binder.
class ObservedRuntimeBackend final : public Render::ModelBatchParticleBackend
{
public:
    using ModelBatchParticleBackend::ModelBatchParticleBackend;
    Result BindTexture(std::int32_t selector) override
    {
        ++binds;
        if (atBind)
            atBind();
        return ModelBatchParticleBackend::BindTexture(selector);
    }
    Result End() override
    {
        if (failEnd)
            return Core::Unexpected(std::make_error_code(std::errc::io_error));
        return ModelBatchParticleBackend::End();
    }
    bool failEnd = false;
    std::function<void()> atBind;
    int binds = 0;
};
class ParticleRuntimeGlTest : public Tests::GlDrawTest
{
protected:
    void SetUp() override
    {
        GlDrawTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
            return;
        m_textures.emplace(Gl());
        const auto fonts = [](int width) -> Core::Expected<std::unique_ptr<Render::Text::IGlyphRasterizer>, std::string>
        {
            return std::unique_ptr<Render::Text::IGlyphRasterizer>(
                std::make_unique<Tests::FakeGlyphRasterizer>(width / 8, 8));
        };
        auto target = Render::Gl::GlFrameTarget::Create(
            Gl(), *m_textures, fonts, [] { return App::PixelSize{Tests::FramebufferSide, Tests::FramebufferSide}; },
            [this] { ++m_presents; }, m_log);
        ASSERT_TRUE(target.HasValue());
        m_target = std::move(target).Value();
        m_world = std::make_unique<Scenes::WorldState>(*m_textures);
        m_world->Runtime().map = 2;
        m_world->Runtime().drawingEnabled = true;
        m_world->Runtime().worldTime = 1234.0f;
        m_world->Actors()[0].dying = true;
        m_world->Actors()[0].position = {13350, 12750, 300};
        std::vector<std::string> skipped;
        ASSERT_TRUE(m_world->Resources()->terrain.Load(Tests::RepoDataDir(), 2, skipped).HasValue());
        ASSERT_EQ(
            m_models.LoadTextures(Tests::RepoDataDir(), Scenes::DaviasLeafTextureSet, *m_textures, m_log).uploaded, 3u);
        // Misma entrada acreditada de carga61d753: dimensiones/componentes provienen del deserializador real.
        ASSERT_EQ(m_models
                      .LoadTextures(Tests::RepoDataDir(), std::span(Scenes::EffectTextureSet).subspan(3, 2),
                                    *m_textures, m_log)
                      .uploaded,
                  2u);
        m_backend = std::make_unique<ObservedRuntimeBackend>(std::ref(m_target->Models()), std::cref(m_models),
                                                             Render::ModelBatchParticleState::ForModelPass());
        m_leaf =
            std::make_unique<Scenes::LeafDrawingServices>(std::cref(m_models), std::ref(*m_backend), std::cref(m_view));
        m_particle = std::make_unique<Scenes::ParticleDrawServices>(std::cref(m_models), std::ref(*m_backend),
                                                                    std::cref(m_view));
    }
    Scenes::SceneContext Context()
    {
        return {.dataRoot = Tests::RepoDataDir(),
                .textures = *m_textures,
                .log = m_log,
                .cloth = &m_cloth,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput,
                .world = m_world.get(),
                .leafWaves = &m_waves,
                .worldTextureModels = &m_models,
                .modelBatch = &m_target->Models(),
                .leafView = &m_view,
                .leafDrawing = m_leaf.get(),
                .particleTrigonometry = &m_math,
                .particleDrawing = m_particle.get()};
    }
    World::Particle& SeedSmoke()
    {
        auto& p = m_world->Resources()->particles.All()[23];
        p = {.alive = true,
             .type = 0x5f2,
             .texture = 0x5f2,
             .subtype = 0,
             .scale = 10,
             .position = m_world->Actors()[0].position,
             .light = {0.3f, 0.6f, 0.9f},
             .life = 10};
        return p;
    }
    void TearDown() override
    {
        if (m_context)
        {
            EXPECT_EQ(Gl().getError(), static_cast<GLenum>(GL_NO_ERROR));
        }
    }
    Tests::RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Debug};
    std::optional<Render::Gl::TextureTable> m_textures;
    Scenes::GameModels m_models;
    std::unique_ptr<Scenes::WorldState> m_world;
    std::unique_ptr<Render::Gl::GlFrameTarget> m_target;
    Render::Models::ClothWorld m_cloth;
    World::GameCamera m_camera;
    World::GameCameraInput m_cameraInput;
    World::PortableLeafWaveEvaluator m_waves;
    World::PortableParticleTrigonometry m_math;
    Render::Models::OriginalMatrix m_view{};
    std::unique_ptr<ObservedRuntimeBackend> m_backend;
    std::unique_ptr<Scenes::LeafDrawingServices> m_leaf;
    std::unique_ptr<Scenes::ParticleDrawServices> m_particle;
    int m_presents = 0;
};
TEST_F(ParticleRuntimeGlTest, SharedBackendDrawsOneParticleQuadPerFrameWithRealMetadata)
{
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds(40)), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    m_target->Present();
    const auto empty = Cells(Tests::FramebufferSide, Tests::FramebufferSide);
    auto& p = SeedSmoke();
    std::size_t vertices = 0;
    for (int frame = 0; frame < 2; ++frame)
    {
        if (frame == 1)
            m_camera.yaw += 30;
        scene.PrepareFrame();
        auto& canvas = m_target->BeginFrame(std::chrono::milliseconds(80 + frame * 40));
        const auto flush = canvas.flushModels;
        canvas.flushModels = [&]
        {
            for (const auto& command : canvas.models.Commands())
                if (command.state.texture && command.state.texture->value == 0x5f2 && command.vertexCount != 0)
                {
                    EXPECT_EQ(command.state.space, Render::ModelCoordinateSpace::Eye);
                    EXPECT_EQ(command.vertexCount, 6u);
                    vertices += command.vertexCount;
                    const auto v = canvas.models.Vertices().subspan(command.firstVertex, command.vertexCount);
                    // Centro analítico: la vista World se aplica una sola vez antes del raster Eye.
                    const float eyeZ = static_cast<float>((static_cast<double>(p.position.x) * m_view[8] +
                                                           static_cast<double>(p.position.y) * m_view[9]) +
                                                          static_cast<double>(p.position.z) * m_view[10] + m_view[11]);
                    EXPECT_FLOAT_EQ(v[0].position.z, eyeZ);
                    EXPECT_FLOAT_EQ(v[0].color.red, p.light.x);
                }
            flush();
        };
        scene.Render(canvas, 0);
        ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
        m_target->Present();
    }
    EXPECT_EQ(vertices, 12u);
    EXPECT_EQ(m_backend->binds, 2);
    EXPECT_EQ(m_presents, 3);
    EXPECT_NE(Cells(Tests::FramebufferSide, Tests::FramebufferSide), empty);
}
TEST_F(ParticleRuntimeGlTest, BinderMutationsReachTheSamePositionColorAndCurrentView)
{
    auto& p = SeedSmoke();
    bool mutated = false;
    m_backend->atBind = [&]
    {
        p.position.z += 17;
        p.light = {0.2f, 0.4f, 0.8f};
        m_view[11] += 11;
        mutated = true;
    };
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.PrepareFrame();
    auto& canvas = m_target->BeginFrame(std::chrono::milliseconds(40000)); // Distinto de T=1234.
    bool observed = false;
    const auto flush = canvas.flushModels;
    canvas.flushModels = [&]
    {
        for (const auto& command : canvas.models.Commands())
            if (command.state.texture && command.state.texture->value == 0x5f2 && command.vertexCount != 0)
            {
                observed = true;
                EXPECT_TRUE(mutated);
                const auto& v = canvas.models.Vertices()[command.firstVertex];
                const double z =
                    (static_cast<double>(p.position.x) * m_view[8] + static_cast<double>(p.position.y) * m_view[9]) +
                    static_cast<double>(p.position.z) * m_view[10] + m_view[11];
                EXPECT_FLOAT_EQ(v.position.z, static_cast<float>(z));
                EXPECT_FLOAT_EQ(v.color.red, 0.2f);
                EXPECT_FLOAT_EQ(v.color.green, 0.4f);
                EXPECT_FLOAT_EQ(v.color.blue, 0.8f);
            }
        flush();
    };
    scene.Render(canvas, 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_TRUE(observed);
    m_target->Present();
    EXPECT_EQ(m_presents, 1);
}
TEST_F(ParticleRuntimeGlTest, MissingImageStopsTheManagerBeforePresentAndBeforeTheNextSlot)
{
    auto& p = SeedSmoke();
    p.texture = 0x7fffffff;
    auto& next = m_world->Resources()->particles.All()[24];
    next = p;
    next.texture = 0x5f2;
    Tests::FakeClock clock;
    Scenes::SceneManager manager([this](Scenes::SceneId) -> std::unique_ptr<Scenes::IScene>
                                 { return std::make_unique<Scenes::GameScene>(Context(), nullptr, &m_models); },
                                 *m_target, clock, {}, m_log);
    ASSERT_TRUE(manager.Start(Scenes::SceneId::Game).HasValue());
    manager.Render(0);
    ASSERT_TRUE(manager.Failure());
    EXPECT_NE(manager.Failure()->find("slot 23, fase 0"), std::string::npos);
    EXPECT_EQ(m_presents, 0);
    EXPECT_EQ(m_backend->binds, 0);
    manager.Render(0);
    EXPECT_EQ(m_presents, 0);
    EXPECT_EQ(m_backend->binds, 0);
}

TEST_F(ParticleRuntimeGlTest, CloudDrawUsesPublishedTimeAndPhysicalSlotAfterSubtractiveMaterial)
{
    auto& p = SeedSmoke();
    p.type = 0x622;
    p.texture = 0x622;
    p.subtype = 10;
    p.rotationFactor = 2;
    p.start.y = 7;
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds(90000)), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    // Slot23 impar: -(T*2)*K +7; T=1234, independiente de90000 del frame.
    const auto expected =
        static_cast<float>(-(1234.0 * 2) * static_cast<double>(std::bit_cast<float>(0x3ca3d70au)) + 7);
    EXPECT_EQ(p.rotation, expected);
    EXPECT_EQ(m_backend->State().blendMode, 4);
    m_target->Present();
    EXPECT_EQ(m_presents, 1);
}
TEST_F(ParticleRuntimeGlTest, MaterialFlagUsesTypeWhileDimensionsUseTheImage)
{
    auto& p = SeedSmoke();
    p.type = 0x7fffffff; // Imagen válida, fila de tipo ausente: error en fase1, no fase0 ni Bind.
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds(40)), 0);
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("slot 23, fase 1"), std::string::npos);
    EXPECT_EQ(m_backend->binds, 0);
}
TEST_F(ParticleRuntimeGlTest, EndFailureIsPropagatedWithoutPresentOrASecondDraw)
{
    SeedSmoke();
    m_backend->failEnd = true;
    Tests::FakeClock clock;
    Scenes::SceneManager manager([this](Scenes::SceneId) -> std::unique_ptr<Scenes::IScene>
                                 { return std::make_unique<Scenes::GameScene>(Context(), nullptr, &m_models); },
                                 *m_target, clock, {}, m_log);
    ASSERT_TRUE(manager.Start(Scenes::SceneId::Game).HasValue());
    manager.Render(0);
    ASSERT_TRUE(manager.Failure());
    EXPECT_NE(
        manager.Failure()->find(std::format("slot 23, fase {}", static_cast<int>(World::ParticleDrawingPhase::End))),
        std::string::npos);
    EXPECT_TRUE(m_backend->IsDrawing());
    EXPECT_EQ(m_backend->binds, 1);
    EXPECT_EQ(m_presents, 0);
    manager.Render(0);
    EXPECT_EQ(m_backend->binds, 1);
    EXPECT_EQ(m_presents, 0);
}
TEST_F(ParticleRuntimeGlTest, ReleaseAndRealReloadKeepPoolWindTimeAndBackendOwners)
{
    Scenes::GameScene scene(Context(), nullptr, &m_models);
    auto* const pool = &m_world->Resources()->particles;
    auto* const time = &*m_world->Runtime().worldTime;
    pool->Wind().wind = {0.7f, -0.3f};
    SeedSmoke();
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds(40)), 0);
    ASSERT_FALSE(scene.Failure());
    m_target->Present();
    ASSERT_TRUE(m_target->Models().InvalidateTextureKnowledge().HasValue());
    m_world->Resources()->Release();
    EXPECT_EQ(&m_world->Resources()->particles, pool);
    EXPECT_FALSE(pool->Any());
    EXPECT_EQ(pool->Wind().wind, (glm::vec2{0.7f, -0.3f}));
    EXPECT_EQ(&*m_world->Runtime().worldTime, time);
    EXPECT_EQ(*time, 1234);
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_world->Resources()->terrain.Load(Tests::RepoDataDir(), 2, skipped).HasValue());
    ASSERT_EQ(
        m_models
            .LoadTextures(Tests::RepoDataDir(), std::span(Scenes::EffectTextureSet).subspan(3, 1), *m_textures, m_log)
            .uploaded,
        1u);
    SeedSmoke();
    scene.PrepareFrame();
    scene.Render(m_target->BeginFrame(std::chrono::milliseconds(80)), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    m_target->Present();
    EXPECT_EQ(m_backend->binds, 2);
    EXPECT_EQ(m_presents, 2);
}
} // namespace
