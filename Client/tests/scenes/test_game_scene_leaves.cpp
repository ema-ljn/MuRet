#include "Scenes/GameScene.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Billboard.h"
#include "Scenes/GameModels.h"
#include "Scenes/LeafDrawingServices.h"
#include "Scenes/DaviasLeafTextureSet.h"
#include "Scenes/LorenciaLeafTextureSet.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Render/SpriteVertex.h"
#include "Data/Models/Model.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/LoadedWorld.h"
#include "World/GameCamera.h"
#include "Scenes/SceneContext.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ILeafWaveEvaluator.h"
#include "World/LeafWeather.h"
#include "World/Leaves.h"
#include "World/PortableLeafWaveEvaluator.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/FakeTextureStore.h"
#include "support/FakeClock.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <functional>
#include <cstddef>
#include <span>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Core::Unexpected;
using Client::World::LeafWave;

class GameSceneLeavesTest : public testing::Test
{
protected:
    GameSceneLeavesTest()
    {
        m_world.Runtime().drawingEnabled = true;
        m_world.Runtime().map = 2;
        m_world.Runtime().worldTime = 40.0f;
        m_world.Actors()[0].dying = true; // No MoveHero ni actores activos: aísla el RNG de Leaf.
        m_world.Actors()[0].position = {1000, 2000, 3000};
        m_cloth.random = Client::Render::Models::CrtRandom(74);
        m_cloth.openCounterAccesses = 1; // Evita los cuatro draws de los cierres lógicos externos.
    }

    Client::Scenes::SceneContext Context(Client::World::ILeafWaveEvaluator* evaluator)
    {
        return {.dataRoot = {},
                .textures = m_textures,
                .log = m_log,
                .cloth = &m_cloth,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput,
                .world = &m_world,
                .leafWaves = evaluator,
                .particleTrigonometry = &m_particleMath};
    }

    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::FakeTextureStore m_textures;
    Client::Scenes::WorldState m_world{m_textures};
    Client::Render::Models::ClothWorld m_cloth;
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_cameraInput;
    Client::World::PortableParticleTrigonometry m_particleMath;
};

// Detecta reset del clima por paso, Phase duplicada, otro pool y otro RNG.
TEST_F(GameSceneLeavesTest, RealEvaluatorCreatesSnowInTheCanonicalPoolAndRetainsWeatherAcrossSteps)
{
    Client::World::PortableLeafWaveEvaluator evaluator;
    m_world.Runtime().leafWeather = {.target = 5, .current = 0, .firstWave = 71, .secondWave = 72, .phase = 1980};
    m_world.Resources()->leaves.All()[80].alive = 0x80;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    const auto& weather = m_world.Runtime().leafWeather;
    EXPECT_EQ(weather.current, 1);
    EXPECT_EQ(weather.firstWave, 30);
    EXPECT_EQ(weather.secondWave, 0);
    EXPECT_EQ(weather.phase, 0);
    EXPECT_EQ(m_cloth.random.State(), 0x272a6989u); // 80*5 draws Leaf + cinco del tail de viento.
    const auto& first = m_world.Resources()->leaves.All()[0];
    EXPECT_EQ(first.alive, 1);
    EXPECT_EQ(first.type, 101);
    EXPECT_EQ(first.scale, 10);
    EXPECT_EQ(first.position, glm::vec3(869, 1767, 3250));
    EXPECT_EQ(first.angles, glm::vec3(-30, 0, 0));
    EXPECT_EQ(first.light, glm::vec3(1));
    EXPECT_EQ(m_world.Resources()->leaves.All()[80].alive, 0x80);
    // Simula el cleanup acreditado de mapa: conserva clima/selector y vuelve a crear el prefijo.
    m_world.Runtime().map = 55; // Bypass real: no limpia ni avanza el estado Leaf.
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure());
    EXPECT_EQ(weather.current, 1);
    EXPECT_EQ(weather.phase, 0);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].position, glm::vec3(869, 1767, 3250));
    m_world.Runtime().map = 2;
    m_world.Runtime().leafSelector = -7;
    m_world.Resources()->Release();
    EXPECT_EQ(m_world.Runtime().leafSelector, -7);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(weather.current, 2);
    EXPECT_EQ(weather.phase, 20);
    EXPECT_EQ(m_world.Runtime().leafSelector, -7);
}

// El spy sólo parametriza la frontera; las aserciones observan stores/lecturas del caller real.
class CallerWaveSpy final : public Client::World::ILeafWaveEvaluator
{
public:
    CallerWaveSpy(Client::Scenes::WorldState& world, Client::Render::Models::ClothWorld& cloth)
        : m_world(world), m_cloth(cloth)
    {
    }
    Expected<void, std::error_code> Evaluate(LeafWave wave, float time) override
    {
        events.push_back(wave == LeafWave::First ? 1 : 3);
        EXPECT_EQ(time, wave == LeafWave::First ? 40 : 80);
        EXPECT_EQ(m_world.Runtime().leafWeather.current, 8);
        EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 71);
        EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1990);
        EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
        EXPECT_EQ(m_cloth.random.State(), 74u);
        return {};
    }
    Expected<std::int32_t, std::error_code> Truncate() override
    {
        const bool first = events.size() == 1;
        events.push_back(first ? 2 : 4);
        EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, first ? 71 : 10);
        EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
        if (events.size() == failAt)
            return Unexpected(std::make_error_code(std::errc::io_error));
        if (first)
            *m_world.Runtime().worldTime = 80;
        return first ? -2 : -1;
    }
    std::size_t failAt = 0;
    std::vector<int> events;

private:
    Client::Scenes::WorldState& m_world;
    Client::Render::Models::ClothWorld& m_cloth;
};

TEST_F(GameSceneLeavesTest, RereadsTimeAndStoresFirstWaveBeforeSecondTruncateThenVisitsPool)
{
    m_world.Runtime().leafWeather = {.target = 10, .current = 7, .firstWave = 71, .secondWave = 72, .phase = 1990};
    CallerWaveSpy spy(m_world, m_cloth);
    Client::Scenes::GameScene scene(Context(&spy), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(spy.events, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 10);
    EXPECT_EQ(m_world.Runtime().leafWeather.secondWave, -20);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 10);
    EXPECT_EQ(m_world.Resources()->leaves.All()[79].alive, 1);
    EXPECT_EQ(m_world.Resources()->leaves.All()[80].alive, 0);
}

TEST_F(GameSceneLeavesTest, FailedSecondConversionKeepsFirstStoreAndCutsThePoolAndHeroSuffix)
{
    m_world.Runtime().leafWeather = {.target = 10, .current = 7, .firstWave = 71, .secondWave = 72, .phase = 1990};
    m_world.Actors()[0].headTargetZ = 19;
    m_world.Actors()[0].dying = false;
    CallerWaveSpy spy(m_world, m_cloth);
    spy.failAt = 4;
    Client::Scenes::GameScene scene(Context(&spy), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_EQ(spy.events, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 8);
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 10);
    EXPECT_EQ(m_world.Runtime().leafWeather.secondWave, 72);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1990);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
    EXPECT_EQ(m_world.Actors()[0].headTargetZ, 19);
}

TEST_F(GameSceneLeavesTest, MissingEvaluatorFailsAfterPreparationWithoutInventingWaves)
{
    m_world.Runtime().leafWeather = {.target = 10, .current = 7, .firstWave = 71, .secondWave = 72, .phase = 1990};
    Client::Scenes::GameScene scene(Context(nullptr), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 8);
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 71);
    EXPECT_EQ(m_world.Runtime().leafWeather.secondWave, 72);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1990);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
}

// Mata truncado a byte del mapa y comparación unsigned de layer, y una consulta fresca adelantada.
TEST_F(GameSceneLeavesTest, DaviasGateUsesThePersistedSignedLayerAndBypassContinuesTheSuffix)
{
    constexpr std::array<std::uint32_t, 10> Layers{0, 2, 3, 4, 9, 10, 0x7fffffffu, 0x80000000u, 0xffffffffu, 256};
    constexpr std::array<bool, 10> Admitted{true, true, false, true, true, false, false, true, true, false};
    Client::World::PortableLeafWaveEvaluator evaluator;
    for (std::size_t i = 0; i < Layers.size(); ++i)
    {
        SCOPED_TRACE(Layers[i]);
        m_world.Resources()->leaves.Clear();
        m_world.Runtime().heroTerrainLayer1 = Layers[i];
        m_world.Runtime().leafWeather = {.target = 5};
        m_world.Actors()[1].active = true;
        m_world.Actors()[1].alpha = 0;
        m_world.Actors()[1].targetAlpha = 1;
        Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
        scene.Step(Client::Input::InputState{});
        ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
        EXPECT_EQ(m_world.Runtime().leafWeather.phase, Admitted[i] ? 20 : 0);
        EXPECT_EQ(m_world.Runtime().leafWeather.current, Admitted[i] ? 1 : 0);
        EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, Admitted[i] ? 1 : 0);
        EXPECT_EQ(m_world.Runtime().heroTerrainLayer1, Layers[i]);
        EXPECT_EQ(m_world.Actors()[1].alpha, 0.05f);
    }
}
TEST_F(GameSceneLeavesTest, GateBypassesRaw258And55ButReportsAdmittedUnsupportedFamilies)
{
    Client::World::PortableLeafWaveEvaluator evaluator;
    for (const std::int32_t map : {258, 55, 1, 33, 34, 54, 0, 3, 7, 30, 31})
    {
        m_world.Runtime().map = map;
        m_world.Runtime().leafWeather = {.target = 5, .firstWave = 71, .phase = 1990};
        Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
        scene.Step(Client::Input::InputState{});
        const bool admitted =
            map == 33 || map == 34 || map == 54 || map == 0 || map == 3 || map == 7 || map == 30 || map == 31;
        if (admitted && map != 34 && map != 0)
            ASSERT_TRUE(scene.Failure()) << map;
        else
            EXPECT_FALSE(scene.Failure()) << map;
        EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, admitted ? 30 : 71);
        EXPECT_EQ(m_world.Runtime().leafWeather.phase, admitted ? 10 : 1990);
        EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, admitted ? 1 : 0);
        m_world.Resources()->leaves.Clear();
    }
}
TEST_F(GameSceneLeavesTest, MissingPublishedTimeStopsAfterPreparationWithoutReadingTheRelativeClock)
{
    m_world.Runtime().worldTime.reset();
    m_world.Runtime().leafWeather = {.target = 10, .current = 7, .firstWave = 71, .phase = 1990};
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Tests::FakeClock relativeClock;
    relativeClock.Advance(Client::Core::Duration{80000000});
    auto context = Context(&evaluator);
    context.clock = &relativeClock; // Un SDL relativo disponible no reemplaza la publicación qT.
    Client::Scenes::GameScene scene(context, nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("qT"), std::string::npos);
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 8);
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 71);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 1990);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
}
TEST_F(GameSceneLeavesTest, ReachedTerrainBindingFailsAfterWavesAndDoesNotVisitTheNextSlot)
{
    m_world.Resources()->leaves.All()[0].alive = 0x80;
    m_world.Resources()->leaves.All()[0].position = {1, 2, 3};
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 20);
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 30);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].position, glm::vec3(1, 2, 3));
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0x80);
    EXPECT_EQ(m_world.Resources()->leaves.All()[1].alive, 0);
    EXPECT_EQ(m_cloth.random.State(), 0x4825ce39u); // Sólo los cinco draws del viento externo.
}
TEST_F(GameSceneLeavesTest, ActiveDaviasLeavesReachTheRealTerrainUpdaterWithTheSameWeatherAndPool)
{
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_world.Resources()->terrain.Load(Client::Tests::RepoDataDir(), 2, skipped).HasValue());
    for (auto& leaf : m_world.Resources()->leaves.All().first(80))
    {
        leaf.alive = 0x80;
        leaf.position = {-1, -1, -100}; // Altura0 acreditada para coordenadas negativas.
        leaf.light = glm::vec3(1);
        leaf.spin = {6, 7, 8};
        leaf.start = {9, 10, 11};
    }
    m_world.Runtime().leafWeather = {.target = 5};
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    const auto& first = m_world.Resources()->leaves.All()[0];
    EXPECT_EQ(first.position, glm::vec3(-1, -1, 0));
    EXPECT_EQ(first.light, glm::vec3(0.95f));
    EXPECT_EQ(first.alive, 0x80);
    EXPECT_EQ(first.spin, glm::vec3(6, 7, 8));
    EXPECT_EQ(first.start, glm::vec3(9, 10, 11));
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 1);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 20);
    EXPECT_EQ(m_cloth.random.State(), 0x4825ce39u);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(m_world.Runtime().leafWeather.current, 2);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 40);
}
class EmptyLeafLibrary final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};
TEST_F(GameSceneLeavesTest, DrawingDaviasStopsAtTheMissing621400ProviderBeforeLegacyLeafDrawing)
{
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_world.Resources()->terrain.Load(Client::Tests::RepoDataDir(), 2, skipped).HasValue());
    Client::World::PortableLeafWaveEvaluator evaluator;
    EmptyLeafLibrary library;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr, &library);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    Client::Tests::RecordingFrameTarget frame;
    scene.Render(frame.BeginFrame(Client::Core::Duration{}), 0);
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("621400"), std::string::npos);
    ASSERT_EQ(frame.frames.size(), 1u);
    EXPECT_FALSE(frame.frames[0].presented);
}
TEST_F(GameSceneLeavesTest, MissingCanonicalRandomReportsTheReachedBindingInsteadOfSkippingLeaves)
{
    Client::World::PortableLeafWaveEvaluator evaluator;
    auto context = Context(&evaluator);
    context.cloth = nullptr;
    Client::Scenes::GameScene scene(context, nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("RNG"), std::string::npos);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 0);
}
TEST_F(GameSceneLeavesTest, FirstUnpublishedFrameKeepsTheImageFirstWaveUntilTheEvaluatorCanRun)
{
    m_world.Runtime().worldTime.reset();
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 30); // DWORD006c8c5c del archivo, no onda calculada.
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 0);
}
TEST_F(GameSceneLeavesTest, PublishedZeroTimeRemainsAValidSample)
{
    m_world.Runtime().worldTime = 0;
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 20);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 1);
}
TEST_F(GameSceneLeavesTest, Map34TraversesAllNineProvenRejectionsWithoutFakeCreationOrExtraRandom)
{
    m_world.Runtime().map = 34;
    m_world.Resources()->leaves.All()[0].type = 777;
    m_world.Resources()->leaves.All()[79].type = -79;
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 20);
    EXPECT_EQ(m_world.Runtime().leafWeather.firstWave, 30);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].alive, 1);
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].light, glm::vec3(1));
    EXPECT_EQ(m_world.Resources()->leaves.All()[0].type, 777);
    EXPECT_EQ(m_world.Resources()->leaves.All()[79].alive, 1);
    EXPECT_EQ(m_world.Resources()->leaves.All()[79].type, -79);
    EXPECT_EQ(m_world.Resources()->leaves.All()[80].alive, 0);
    EXPECT_EQ(m_cloth.random.State(), 0x4825ce39u); // C6..C9 rechazados no agregan draws.
}
TEST_F(GameSceneLeavesTest, RealDurableDrawingProviderConsumesTheActualPassAcrossTwoFrames)
{
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_world.Resources()->terrain.Load(Client::Tests::RepoDataDir(), 2, skipped).HasValue());
    Client::Scenes::GameModels models;
    const auto loaded =
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::DaviasLeafTextureSet, m_textures, m_log);
    ASSERT_EQ(loaded.uploaded, 3u);
    Client::Tests::RecordingFrameTarget frame;
    auto& firstCanvas = frame.BeginFrame(Client::Core::Duration{});
    auto& batch = firstCanvas.models;
    Client::Render::Models::OriginalMatrix view{}; // Almacenamiento; Render publica la vista real antes de usarlo.
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::PortableLeafWaveEvaluator evaluator;
    auto context = Context(&evaluator);
    context.modelBatch = &batch;
    context.leafView = &view;
    context.leafDrawing = &provider;
    Client::Scenes::GameScene scene(context, nullptr, &models);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    scene.PrepareFrame();
    scene.Render(firstCanvas, 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(view, Client::Render::ViewRows(batch.Pass().view));
    frame.Present();
    auto& secondCanvas = frame.BeginFrame(Client::Core::Duration{40000000});
    m_world.Actors()[0].position.x += 400;
    secondCanvas.models.SetState({.texture = std::nullopt, .blend = Client::Render::ModelBlend::Alpha});
    scene.PrepareFrame();
    scene.Render(secondCanvas, 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(view, Client::Render::ViewRows(batch.Pass().view));
    frame.Present();
    EXPECT_EQ(frame.frames.size(), 2u);
}

TEST_F(GameSceneLeavesTest, NonDaviasEmptyPoolContinuesAndAnActiveLeafUsesTheRealWorldQuad)
{
    m_world.Runtime().map = 0;
    std::vector<std::string> skipped;
    ASSERT_TRUE(m_world.Resources()->terrain.Load(Client::Tests::RepoDataDir(), 0, skipped).HasValue());
    Client::Scenes::GameModels models;
    // World1 conserva100RGBA del OZT y reporta los dos JPG ausentes; el dibujo no requiere101.
    ASSERT_EQ(
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::LorenciaLeafTextureSet, m_textures, m_log)
            .uploaded,
        1u);
    Client::Tests::RecordingFrameTarget frame;
    auto& batch = frame.Models();
    Client::Render::Models::OriginalMatrix view{};
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(models), std::ref(backend), std::cref(view)};
    Client::World::PortableLeafWaveEvaluator evaluator;
    auto context = Context(&evaluator);
    context.modelBatch = &batch;
    context.leafView = &view;
    context.leafDrawing = &provider;
    Client::Scenes::GameScene scene(context, nullptr, &models);
    scene.PrepareFrame();
    scene.Render(frame.BeginFrame(Client::Core::Duration{}), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    frame.Present();
    auto& leaf = m_world.Resources()->leaves.All()[0];
    leaf = {.alive = 0x80, .type = 100, .position = {1000, 2000, 3000}, .light = {0.25f, 0.5f, 0.75f}};
    scene.PrepareFrame();
    scene.Render(frame.BeginFrame(Client::Core::Duration{}), 0);
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    EXPECT_EQ(backend.State().textureSelector, std::optional<std::int32_t>(100));
    EXPECT_EQ(backend.State().color, Client::Render::Color(1, 1, 1, 1));
    EXPECT_FALSE(backend.IsDrawing());
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
    frame.Present();
    std::size_t worldVertices = 0;
    for (const auto& event : frame.frames.back().events)
        for (const auto& command : event.modelCommands)
            if (command.state.texture && command.state.texture->value == 100)
            {
                // El binder puede publicar un prefijo Eye sin vértices; el quad debe ser World.
                if (command.vertexCount != 0)
                {
                    EXPECT_EQ(command.state.space, Client::Render::ModelCoordinateSpace::World);
                }
                worldVertices += command.vertexCount;
            }
    EXPECT_EQ(worldVertices, 6u);
}

TEST_F(GameSceneLeavesTest, MissingCameraInMapZeroPreservesTheFourthDrawPrefixAndCutsTheRemainingPool)
{
    m_world.Runtime().map = 0;
    Client::World::PortableLeafWaveEvaluator evaluator;
    auto context = Context(&evaluator);
    context.camera = nullptr;
    Client::Scenes::GameScene scene(context, nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    const auto& leaf = m_world.Resources()->leaves.All()[0];
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(leaf.start, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.x), 0xc1033333u);
    EXPECT_EQ(m_cloth.random.State(), 0x4db430cdu); // Cuatro C3 +cinco del RAII ClothWorldStep heredado.
    EXPECT_EQ(m_world.Resources()->leaves.All()[1].alive, 0);
    EXPECT_EQ(m_world.Runtime().leafWeather.phase, 20);
}

TEST_F(GameSceneLeavesTest, AnInitiallyOutOfWindowHeroViolatesTheCallerPreconditionBeforeBorrowingActors)
{
    auto& state = m_world.Runtime();
    state.map = 0;
    state.hero = m_world.Actors().size();
    state.leafWeather = {.target = 10, .current = 7, .firstWave = 71, .secondWave = 72, .phase = 1990};
    auto& leaf = m_world.Resources()->leaves.All()[0];
    leaf.type = -17;
    leaf.position = {7, 8, 9};
    leaf.light = {0.25f, 0.5f, 0.75f};
    CallerWaveSpy evaluator(m_world, m_cloth);
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure());
    EXPECT_NE(scene.Failure()->find("precondición del caller"), std::string::npos);
    EXPECT_EQ(state.hero, m_world.Actors().size());
    EXPECT_EQ(state.leafWeather.target, 10);
    EXPECT_EQ(state.leafWeather.current, 7);
    EXPECT_EQ(state.leafWeather.firstWave, 71);
    EXPECT_EQ(state.leafWeather.secondWave, 72);
    EXPECT_EQ(state.leafWeather.phase, 1990);
    EXPECT_TRUE(evaluator.events.empty());
    EXPECT_EQ(leaf.alive, 0);
    EXPECT_EQ(leaf.type, -17);
    EXPECT_EQ(leaf.position, glm::vec3(7, 8, 9));
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(m_world.Resources()->leaves.All()[1].alive, 0);
    // No hay draw Leaf; los cinco draws del RAII ClothWorldStep heredado siguen ocurriendo.
    EXPECT_EQ(m_cloth.random.State(), 0x4825ce39u);
}

TEST_F(GameSceneLeavesTest, MapZeroKeepsThePublishedCameraAcrossLogicalStepsAndResourceRelease)
{
    m_world.Runtime().map = 0;
    const Client::World::GameCameraInput previousDraw{.hero = {14350, 12050, 165}, .cellAttribute = 0x01};
    static_cast<void>(Client::World::StepGameCamera(m_camera, previousDraw));
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_camera.publishedY), 0x46313b93u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_world.Resources()->leaves.All()[0].velocity.x), 0x41366667u);
    m_world.Actors()[0].position.y += 256;
    m_world.Resources()->Release();
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_camera.publishedY), 0x46313b93u);
    const Client::World::GameCameraInput nextDraw{.hero = m_world.Actors()[0].position, .cellAttribute = 0x01};
    static_cast<void>(Client::World::GameCameraOf(m_camera, nextDraw));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_camera.publishedY), 0x46313b93u);
    const auto newFrame = Client::World::StepGameCamera(m_camera, nextDraw);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(m_camera.publishedY), std::bit_cast<std::uint32_t>(newFrame.position.y));
    EXPECT_NE(std::bit_cast<std::uint32_t>(m_camera.publishedY), 0x46313b93u);
}

TEST_F(GameSceneLeavesTest, MapZeroGenericDriftUsesTheExistingKernelBeforeTheRemainingCreatorsAndSharedWind)
{
    m_world.Runtime().map = 0;
    auto& leaf = m_world.Resources()->leaves.All()[0];
    leaf = {.alive = 0x80,
            .type = 100,
            .position = {100, 200, -2.5f},
            .angles = {10, 20, 2.94f},
            .velocity = {1, 2, 3},
            .spin = {0, 1, -3},
            .start = {100, 200, -2.5f}};
    Client::World::PortableLeafWaveEvaluator evaluator;
    Client::Scenes::GameScene scene(Context(&evaluator), nullptr);
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure()) << scene.Failure().value_or("");
    // Literales racionales L5/5301a0: el Z retenido no se reemplaza por el store f32.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.x), 0x42ca0000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.y), 0x434a8000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.z), 0xb2000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.angles.z), 0x33700000u);
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(m_world.Resources()->leaves.All()[1].type, 100);
    EXPECT_EQ(m_cloth.random.State(), 0x2df42964u); // 6 draws U3 +79*9 C3 +5 del viento, cálculo LCG independiente.
}

TEST_F(GameSceneLeavesTest, RecordingCanvasConsumesStateOnlyCommandsWithoutInventingGeometry)
{
    Client::Tests::RecordingFrameTarget frame;
    auto& canvas = frame.BeginFrame(Client::Core::Duration{});
    canvas.models.ApplyState({.texture = std::nullopt, .blend = Client::Render::ModelBlend::Additive});
    ASSERT_TRUE(canvas.models.Vertices().empty());
    ASSERT_FALSE(canvas.models.Commands().empty());
    canvas.flushModels();
    frame.Present();
    std::size_t modelEvents = 0;
    for (const auto& event : frame.frames.back().events)
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            ++modelEvents;
            EXPECT_FALSE(event.modelCommands.empty());
            EXPECT_TRUE(event.modelVertices.empty());
            ASSERT_TRUE(event.pass);
        }
    EXPECT_EQ(modelEvents, 1u);
    EXPECT_TRUE(canvas.models.Commands().empty());
}
} // namespace
