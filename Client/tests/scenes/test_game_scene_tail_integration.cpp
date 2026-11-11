#include "Scenes/GameScene.h"
#include "App/GameLoop.h"
#include "App/PacingMode.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Input/InputState.h"
#include "Scenes/SceneManager.h"
#include "Scenes/IScene.h"
#include "Scenes/WorldLoadingScene.h"
#include "World/NoriaObjectStep.h"
#include "World/WorldObjects.h"
#include "World/ObjectModelStepState.h"
#include "Render/Models/Animation.h"
#include "Render/SpriteVertex.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/MapLoad.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/FaunaState.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneContext.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "World/TerrainLighting.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <optional>
#include <ratio>

namespace
{
constexpr Client::Render::Color MovingLightTint{0.13f, 0.27f, 0.39f, 1};
constexpr std::size_t MovingLightVertexCount = 20 * 6;

std::size_t MovingLightVertices(const Client::Tests::RecordedFrame& recorded)
{
    std::size_t count = 0;
    for (const auto& event : recorded.events)
        for (const auto& command : event.modelCommands)
        {
            if (command.state.texture != Client::Render::TextureSlot{0x5c6})
                continue;
            for (std::size_t i = 0; i < command.vertexCount; ++i)
            {
                const auto& color = event.modelVertices[command.firstVertex + i].color;
                if (color.red == MovingLightTint.red && color.green == MovingLightTint.green &&
                    color.blue == MovingLightTint.blue)
                    ++count;
            }
        }
    return count;
}

struct FrameFixture
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Tests::FakeTextureStore textures;
    Client::Tests::FakeClock clock;
    Client::Render::Models::ClothWorld cloth;
    Client::Render::Models::OriginalFrameCarry itemFrames;
    Client::Scenes::WorldState world;
    Client::Scenes::GameModels models;
    Client::Tests::RecordingFrameTarget target;
    std::unique_ptr<Client::Scenes::GameScene> scene;
};
class GameSceneTailIntegrationTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        m_frame = std::make_unique<FrameFixture>();
        auto& frame = *m_frame;
        const auto models = frame.models.Load(m_dataRoot, frame.textures, frame.log);
        ASSERT_TRUE(models.HasValue()) << models.Error();
        frame.world.Runtime().map = 0;
        const auto loaded =
            Client::Scenes::LoadMap(0, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random});
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        frame.world.Runtime().drawingEnabled = true;
        frame.world.Actors()[0].position = {13550, 12550, 0};
        frame.scene =
            std::make_unique<Client::Scenes::GameScene>(Client::Scenes::SceneContext{.dataRoot = m_dataRoot,
                                                                                     .textures = frame.textures,
                                                                                     .log = frame.log,
                                                                                     .clock = &frame.clock,
                                                                                     .cloth = &frame.cloth,
                                                                                     .world = &frame.world},
                                                        nullptr, &frame.models, nullptr, &frame.models.TextureSlots());
        ASSERT_TRUE(frame.scene->Enter().HasValue());
    }
    void PublishFrame(Client::Core::Duration now)
    {
        auto& frame = *m_frame;
        if (frame.itemFrames.FramesAt(now, Client::Render::Models::OriginalFramePace::ByTheClock) == 0)
            return;
        const float clock = static_cast<float>(std::chrono::duration<double, std::milli>(now).count());
        Client::Scenes::UpdateGameItemGlowPulse(frame.world.Runtime().itemRender, clock);
    }
    void Render()
    {
        PublishFrame(m_frame->clock.Now());
        m_frame->scene->PrepareFrame();
        auto& canvas = m_frame->target.BeginFrame(m_frame->clock.Now());
        m_frame->scene->Render(canvas, 0);
        m_frame->target.Present();
    }
    Client::Scenes::SceneContext Context()
    {
        auto& frame = *m_frame;
        return {.dataRoot = m_dataRoot,
                .textures = frame.textures,
                .log = frame.log,
                .clock = &frame.clock,
                .cloth = &frame.cloth,
                .world = &frame.world};
    }
    std::unique_ptr<Client::Scenes::SceneManager> MakeManager(Client::Scenes::SceneId first)
    {
        auto& frame = *m_frame;
        auto manager = std::make_unique<Client::Scenes::SceneManager>(
            [&](Client::Scenes::SceneId id) -> std::unique_ptr<Client::Scenes::IScene>
            {
                if (id == Client::Scenes::SceneId::Game)
                    return std::move(frame.scene);
                if (id == Client::Scenes::SceneId::WorldLoading)
                    return std::make_unique<Client::Scenes::WorldLoadingScene>(Context());
                return nullptr;
            },
            frame.target, frame.clock, []() -> Client::Core::Expected<void, std::string> { return {}; }, frame.log,
            []() -> std::optional<Client::Scenes::SceneId> { return std::nullopt; },
            [this](Client::Core::Duration now) { PublishFrame(now); });
        EXPECT_TRUE(manager->Start(first).HasValue());
        return manager;
    }
    void MovingLight(float offset = 0)
    {
        auto& frame = *m_frame;
        auto& effect = frame.world.Map().effects.Effects().back();
        effect.alive = true;
        effect.type = 0x5e8;
        effect.transform.scale = 1;
        effect.transform.position = frame.world.Actors()[0].position;
        effect.transform.position.x += offset;
        effect.light = {MovingLightTint.red, MovingLightTint.green, MovingLightTint.blue};
    }
    void IntermediateRender()
    {
        auto& frame = *m_frame;
        PublishFrame(frame.clock.Now());
        frame.scene->PrepareFrame();
        const auto random = frame.cloth.random.State();
        const auto lighting =
            std::make_unique<Client::World::TerrainLighting>(frame.world.DynamicLighting().Lighting());
        auto& canvas = frame.target.BeginFrame(frame.clock.Now());
        frame.scene->Render(canvas, 0.5f);
        ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
        EXPECT_EQ(frame.cloth.random.State(), random);
        EXPECT_EQ(frame.world.DynamicLighting().Lighting().cells, lighting->cells);
        frame.target.Present();
    }
    std::unique_ptr<FrameFixture> m_frame;
};
} // namespace

TEST_F(GameSceneTailIntegrationTest, LiveMovingLightReceivesTheRealSpriteAndLightOwners)
{
    auto& frame = *m_frame;
    auto& effect = frame.world.Map().effects.Effects().back();
    effect.alive = true;
    effect.type = 0x5e8;
    effect.transform.scale = 1;
    effect.transform.position = frame.world.Actors()[0].position;
    effect.light = {0.1f, 0.2f, 0.3f};
    const auto before = std::make_unique<Client::World::TerrainLighting>(frame.world.DynamicLighting().Lighting());
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    EXPECT_NE(frame.world.DynamicLighting().Lighting().cells, before->cells);
    EXPECT_TRUE(frame.target.frames.back().presented);
    const auto retained = std::make_unique<Client::World::TerrainLighting>(frame.world.DynamicLighting().Lighting());
    frame.scene->PrepareFrame();
    const auto random = frame.cloth.random.State();
    auto& canvas = frame.target.BeginFrame(frame.clock.Now());
    frame.scene->Render(canvas, 0.5f);
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    EXPECT_EQ(frame.world.DynamicLighting().Lighting().cells, retained->cells);
    EXPECT_EQ(frame.cloth.random.State(), random);
}

TEST_F(GameSceneTailIntegrationTest, TheFramePulsePrecedesEvenAFailingWorldDraw)
{
    auto& frame = *m_frame;
    auto& items = frame.world.Runtime().itemRender;
    EXPECT_EQ(items.glowPulse, 0);
    // Sin el mapping, 0x6371a0 corta (el mapa no está cargado) después del publisher 639482.
    frame.world.Map().terrain.mapping.reset();
    Render();
    ASSERT_TRUE(frame.scene->Failure().has_value());
    EXPECT_NE(frame.scene->Failure()->find("no está cargado"), std::string::npos) << *frame.scene->Failure();
    EXPECT_FLOAT_EQ(items.glowPulse, 0.6f);
    EXPECT_EQ(items.specialPulse.x, 0.5f);
    EXPECT_EQ(items.specialPulse.y, 0.01f);
}

TEST_F(GameSceneTailIntegrationTest, IntermediateDrawLeavesFloorJointLightingAndRandomOwnersUnchanged)
{
    auto& frame = *m_frame;
    auto& floor = frame.world.DrawTail().floorItems[0];
    floor.alive = true;
    floor.type = 0x212;
    floor.scale = 1;
    floor.alpha = 1;
    floor.position = frame.world.Actors()[0].position;
    floor.light = {1, 1, 1};
    auto& joint = frame.world.DrawTail().joints[0];
    joint.alive = true;
    joint.type = 0x619;
    joint.subtype = 9;
    joint.texture = 0x613;
    joint.count = 3;
    joint.maximumCount = 5;
    joint.drawEnabled = 3;
    joint.ownerContext = &frame.world.Fauna().Birds()[0];
    joint.light = {0.5f, 0.5f, 0.5f};
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    floor.screenX = -12345;
    floor.seen = false;
    const auto color = joint.light;
    frame.world.Runtime().itemRender.glowPulse = 0.125f;
    const auto light = std::make_unique<Client::World::TerrainLighting>(frame.world.DynamicLighting().Lighting());
    frame.scene->PrepareFrame();
    const auto random = frame.cloth.random.State();
    auto& canvas = frame.target.BeginFrame(frame.clock.Now());
    frame.scene->Render(canvas, 0.5f);
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    EXPECT_EQ(floor.screenX, -12345);
    EXPECT_FALSE(floor.seen);
    EXPECT_EQ(joint.light, color);
    EXPECT_EQ(frame.world.Runtime().itemRender.glowPulse, 0.125f);
    EXPECT_EQ(frame.cloth.random.State(), random);
    EXPECT_EQ(frame.world.DynamicLighting().Lighting().cells, light->cells);
}

TEST_F(GameSceneTailIntegrationTest, KalimaWaterRunsBothModesWithAnExplicitEmptyObjectTable)
{
    auto& frame = *m_frame;
    frame.world.Runtime().map = 24;
    const auto loaded =
        Client::Scenes::LoadMap(24, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_NE(frame.world.Map().kalima, nullptr);
    // Aísla la cola de agua; no acredita los cuerpos por tipo de los objetos de este mapa.
    frame.world.Map().objects.clear();
    frame.world.Actors()[0].position = {12850, 12850, 400};
    frame.world.Actors()[0].cellX = 128;
    frame.world.Actors()[0].cellY = 128;
    frame.cloth.logicSteps = 1;
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    const bool selected = frame.world.Map().kalima->Waves().selected;
    const auto random = frame.cloth.random.State();
    frame.scene->PrepareFrame();
    const auto beforeReplay = frame.cloth.random.State();
    auto& canvas = frame.target.BeginFrame(frame.clock.Now());
    frame.scene->Render(canvas, 0.5f);
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    EXPECT_EQ(frame.world.Map().kalima->Waves().selected, selected);
    EXPECT_EQ(frame.cloth.random.State(), beforeReplay);
    EXPECT_NE(random, 0u);
}

TEST_F(GameSceneTailIntegrationTest, ReloadingTheSameMapInvalidatesTheGroundReplayBeforeAnIntermediateDraw)
{
    auto& frame = *m_frame;
    auto arrival = [&](float offset)
    {
        auto position = frame.world.Actors()[0].position;
        position.x += offset;
        const auto index = frame.world.Map().effects.Create(0x61f, position, {}, {1, 1, 1}, 1);
        EXPECT_TRUE(index.has_value());
        if (index)
            frame.world.Map().effects.Effects()[*index].seen = true;
    };
    auto firstRingX = [](const Client::Tests::RecordedFrame& recorded) -> std::optional<float>
    {
        for (const auto& event : recorded.events)
            for (const auto& command : event.modelCommands)
                if (command.vertexCount > 0 && command.state.texture == Client::Render::TextureSlot{0x61f})
                    return event.modelVertices[command.firstVertex].position.x;
        return std::nullopt;
    };
    arrival(0);
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    const auto previousX = firstRingX(frame.target.frames.back());
    ASSERT_TRUE(previousX.has_value());
    const auto loaded =
        Client::Scenes::LoadMap(0, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random});
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    arrival(100);
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    const auto currentX = firstRingX(frame.target.frames.back());
    ASSERT_TRUE(currentX.has_value());
    EXPECT_FLOAT_EQ(*currentX - *previousX, 100);
}

TEST_F(GameSceneTailIntegrationTest, ReloadInvalidatesDurableNoriaMaterialsBeforeTheFirstProjectedModelLoad)
{
    auto& frame = *m_frame;
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    for (const std::uint8_t map : std::array<std::uint8_t, 2>{0, 2})
    {
        ASSERT_TRUE(
            Client::Scenes::LoadMap(3, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random})
                .HasValue());
        auto& state = frame.world.Runtime().objectModelState;
        for (const std::int16_t type : std::array<std::int16_t, 2>{0x2a, 0x2b})
        {
            Client::World::WorldObject object;
            object.type = type;
            ASSERT_TRUE(Client::World::StepNoriaObject(object, frame.cloth.random, {.modelState = &state}).HasValue());
            ASSERT_EQ(state.unlitMeshes.at(type), 0);
        }
        ASSERT_TRUE(Client::Scenes::LoadMap(
                        map, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random})
                        .HasValue());
        ASSERT_NO_FATAL_FAILURE(IntermediateRender());
        EXPECT_TRUE(state.unlitMeshes.empty()) << "la recarga debe limpiar el owner durable, no sólo la proyección";
        frame.clock.Advance(Client::Render::Models::OriginalFrameTime);
        Render();
        ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
        EXPECT_TRUE(state.unlitMeshes.empty())
            << "el cache hit del siguiente original no debe recuperar el material viejo";
    }
}

TEST_F(GameSceneTailIntegrationTest, HeldGameLoopFramePublishesThePulseBeforeTheWaitSkipsRender)
{
    auto& frame = *m_frame;
    Render();
    const auto frames = frame.target.frames.size();
    ASSERT_GT(frames, 0u);
    frame.world.Runtime().warpCounter = 31;
    auto manager = MakeManager(Client::Scenes::SceneId::Game);
    frame.clock.Advance(std::chrono::milliseconds{1000});
    Client::Input::InputState input;
    Client::App::GameLoop loop(frame.clock, *manager, Client::App::PacingMode::Decoupled, 0);
    EXPECT_EQ(loop.RunFrame(Client::App::WindowState{}, input), 0);
    EXPECT_EQ(frame.target.frames.size(), frames);
    EXPECT_TRUE(manager->HoldsTheLastFrame());
    EXPECT_FALSE(manager->Failure());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(frame.world.Runtime().itemRender.glowPulse), 0x3ef913dcu);
}

TEST_F(GameSceneTailIntegrationTest, ADisabledWorldPublishesWithoutReadingTheWarpCounterOrRendering)
{
    auto& frame = *m_frame;
    Render();
    const auto frames = frame.target.frames.size();
    frame.world.Runtime().drawingEnabled = false;
    frame.world.Runtime().warpCounter = 123;
    auto manager = MakeManager(Client::Scenes::SceneId::Game);
    frame.clock.Advance(std::chrono::milliseconds{1000});
    const auto random = frame.cloth.random.State();
    Client::Input::InputState input;
    Client::App::GameLoop loop(frame.clock, *manager, Client::App::PacingMode::Decoupled, 0);
    EXPECT_EQ(loop.RunFrame(Client::App::WindowState{}, input), 0);
    EXPECT_TRUE(manager->HoldsTheLastFrame());
    EXPECT_EQ(frame.target.frames.size(), frames);
    EXPECT_EQ(frame.cloth.random.State(), random);
    EXPECT_EQ(frame.world.Runtime().warpCounter, 123);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(frame.world.Runtime().itemRender.glowPulse), 0x3ef913dcu);
}

TEST_F(GameSceneTailIntegrationTest, TheOrchestratorPublishesDuringTheRealWorldLoadingSceneToo)
{
    auto& frame = *m_frame;
    auto manager = MakeManager(Client::Scenes::SceneId::WorldLoading);
    frame.clock.Advance(std::chrono::milliseconds{1000});
    Client::Input::InputState input;
    Client::App::GameLoop loop(frame.clock, *manager, Client::App::PacingMode::Decoupled, 0);
    EXPECT_EQ(loop.RunFrame(Client::App::WindowState{}, input), 0);
    ASSERT_EQ(frame.target.frames.size(), 1u);
    EXPECT_TRUE(frame.target.frames[0].presented);
    EXPECT_EQ(frame.target.frames[0].events.size(), 4u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(frame.world.Runtime().itemRender.glowPulse), 0x3ef913dcu);
}

TEST_F(GameSceneTailIntegrationTest, ReloadedProjectedMovingLightKeepsItsSpritesWithoutDuplicatingTheReplay)
{
    auto& frame = *m_frame;
    MovingLight();
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    ASSERT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
    ASSERT_TRUE(
        Client::Scenes::LoadMap(0, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random})
            .HasValue());
    MovingLight(100);
    ASSERT_NO_FATAL_FAILURE(IntermediateRender());
    EXPECT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
    ASSERT_NO_FATAL_FAILURE(IntermediateRender());
    EXPECT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
    frame.clock.Advance(Client::Render::Models::OriginalFrameTime);
    Render();
    ASSERT_FALSE(frame.scene->Failure().has_value()) << *frame.scene->Failure();
    EXPECT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
    ASSERT_NO_FATAL_FAILURE(IntermediateRender());
    EXPECT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
}

TEST_F(GameSceneTailIntegrationTest, FreshProjectedMovingLightSurvivesTheSecondKalimaSpritePass)
{
    auto& frame = *m_frame;
    Render();
    ASSERT_TRUE(
        Client::Scenes::LoadMap(24, {m_dataRoot, frame.world, Client::Scenes::SceneId::Game, 0, frame.cloth.random})
            .HasValue());
    frame.world.Map().objects.clear(); // Aislamiento explícito de la cola; no acredita objetos Kalima.
    frame.world.Actors()[0].position = {9850, 9850, 400};
    frame.world.Actors()[0].cellX = 98;
    frame.world.Actors()[0].cellY = 98;
    frame.cloth.logicSteps = 1;
    MovingLight();
    ASSERT_NO_FATAL_FAILURE(IntermediateRender());
    EXPECT_EQ(MovingLightVertices(frame.target.frames.back()), MovingLightVertexCount);
}

TEST_F(GameSceneTailIntegrationTest, TheGlobalPrefixKeepsTheDurablePulseDuringAHeldIntermediateFrame)
{
    auto& frame = *m_frame;
    frame.world.Runtime().warpCounter = 31;
    auto manager = MakeManager(Client::Scenes::SceneId::Game);
    ASSERT_TRUE(manager->HoldsTheLastFrame());
    EXPECT_FLOAT_EQ(frame.world.Runtime().itemRender.glowPulse, 0.6f);
    frame.world.Runtime().itemRender.glowPulse = 0.125f;
    frame.clock.Advance(std::chrono::milliseconds{1});
    manager->Poll();
    ASSERT_TRUE(manager->HoldsTheLastFrame());
    EXPECT_FLOAT_EQ(frame.world.Runtime().itemRender.glowPulse, 0.125f);
    static_cast<void>(manager->HoldsTheLastFrame()); // La consulta repetida del mismo frame tampoco publica dos veces.
    EXPECT_FLOAT_EQ(frame.world.Runtime().itemRender.glowPulse, 0.125f);
    frame.clock.Advance(Client::Render::Models::OriginalFrameTime - std::chrono::milliseconds{1});
    manager->Poll();
    ASSERT_TRUE(manager->HoldsTheLastFrame());
    EXPECT_NE(frame.world.Runtime().itemRender.glowPulse, 0.125f);
    EXPECT_TRUE(frame.target.frames.empty());
}
