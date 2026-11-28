#include "Scenes/GameScene.h"
#include "Tables/GameData.h"
#include "Assets/GameModels.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Session/MapLoad.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldMap.h"
#include "Session/WorldEntry.h"
#include "Session/IWorldEntryPublisher.h"
#include "Session/IWorldTransitionPublisher.h"
#include "Session/WorldTransitions.h"
#include "Protocol/Movement/World.h"
#include "FrontEnd/SelectionState.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Input/InputState.h"
#include "Camera/GameCamera.h"
#include "Models/Cloth.h"
#include "Render/ModelVertex.h"
#include "Render/TextureSlot.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Session/ModuleStates.h"
#include "support/GoldenDir.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/LetterMeasure.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <cstddef>

namespace
{
class CompleteGameFrameTest : public Client::Tests::GoldenDataTest
{
};
class CompleteWorldEntryTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
{
};

std::vector<Client::Render::ModelVertex> FlareVertices(const Client::Tests::RecordedFrame& frame)
{
    std::vector<Client::Render::ModelVertex> vertices;
    for (const auto& event : frame.events)
        for (const auto& command : event.modelCommands)
            if (command.state.texture == Client::Render::TextureSlot{0x5ac})
                for (std::size_t i = 0; i < command.vertexCount; ++i)
                    vertices.push_back(event.modelVertices[command.firstVertex + i]);
    return vertices;
}
class Publisher final : public Client::Scenes::IWorldEntryPublisher, public Client::Scenes::IWorldTransitionPublisher
{
public:
    void PublishWorldEntry() override {}
    void ConfirmTeleport() override
    {
        ++confirmations;
    }
    void RequestCrywolfState() override {}
    int confirmations = 0;
};

TEST_F(CompleteGameFrameTest, EmptyTailPoolsAllowTheWholeWorldPassInBothReferenceMaps)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Client::Tests::FakeTextureStore textures;
        Client::Tests::FakeClock clock;
        Client::Models::ClothWorld cloth;
        Client::Kernel::WorldState world;
        Client::Session::ModuleStates states;
        Client::Camera::GameCamera camera;
        Client::Camera::GameCameraInput cameraInput;
        auto data = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        Client::Assets::GameModels models;
        const auto modelsLoaded = models.Load(m_dataRoot, textures, log);
        ASSERT_TRUE(modelsLoaded.HasValue()) << modelsLoaded.Error();
        world.Runtime().map = map;
        const auto loaded = Client::Scenes::LoadMap(
            map, {m_dataRoot, world, states.terrain, Client::Kernel::SceneId::Game, 0, cloth.random});
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        world.Runtime().drawingEnabled = true;
        world.Actors()[0].position = {13550.0f, 12550.0f, 0.0f};
        auto& butterfly = world.Fauna().Birds()[0];
        butterfly.active = 1;
        butterfly.model = 0xae;
        butterfly.bodyScale = 1;
        butterfly.scale = 1;
        butterfly.alpha = 1;
        butterfly.position = world.Actors()[0].position;
        auto& mirror = world.Map().effects.Effects().back();
        mirror.alive = true;
        mirror.type = 0x135;
        mirror.transform.scale = 1;
        mirror.transform.position = butterfly.position;
        mirror.transform.position.z = 100;
        mirror.start.z = 20;
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = textures,
                                                   .log = log,
                                                   .clock = &clock,
                                                   .cloth = &cloth,
                                                   .camera = &camera,
                                                   .cameraInput = &cameraInput,
                                                   .world = &world,
                                                   .states = &states};
        Client::Scenes::GameScene scene(context, nullptr, &models, &data.Value().items->table, &models.TextureSlots(),
                                        &*data.Value().gates, &*data.Value().skills);
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;
        for (int frame = 0; frame < 2; ++frame)
        {
            scene.PrepareFrame();
            ASSERT_FALSE(scene.HoldsTheLastFrame());
            auto& canvas = target.BeginFrame(clock.Now());
            scene.Render(canvas, 0.0f);
            ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
            target.Present();
            if (frame == 0)
            {
                if (map == 0)
                {
                    ASSERT_TRUE(states.assets.fauna.TextureTable().Metadata(0x64).has_value());
                }
                ASSERT_FALSE(world.PoseCarry().bones.empty());
                const auto bones = world.PoseCarry().bones;
                ASSERT_TRUE(mirror.seen);
                const float mirrorHeight = mirror.transform.position.z;
                const auto originalFlares = FlareVertices(target.frames[0]);
                ASSERT_FALSE(originalFlares.empty());
                scene.PrepareFrame();
                const auto random = cloth.random.State();
                auto& intermediate = target.BeginFrame(clock.Now());
                scene.Render(intermediate, 0.5f);
                ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
                target.Present();
                EXPECT_EQ(world.PoseCarry().bones, bones);
                EXPECT_EQ(cloth.random.State(), random);
                EXPECT_FLOAT_EQ(mirror.transform.position.z, mirrorHeight);
                const auto projectedFlares = FlareVertices(target.frames[1]);
                ASSERT_EQ(projectedFlares.size(), originalFlares.size());
                for (std::size_t vertex = 0; vertex < originalFlares.size(); ++vertex)
                {
                    EXPECT_EQ(projectedFlares[vertex].position, originalFlares[vertex].position);
                    EXPECT_EQ(projectedFlares[vertex].uv, originalFlares[vertex].uv);
                    EXPECT_EQ(projectedFlares[vertex].color, originalFlares[vertex].color);
                }
            }
            clock.Advance(std::chrono::milliseconds(47));
            // Los dos pasos de 40 ms que el bucle corre antes del dibujo siguiente, que así es un frame del
            // original (OriginalFramesOfTheSteps). La mariposa del arreglo no la creó 0x446120 y no tiene el
            // modelo preparado, así que se apaga antes de que el paso la mueva.
            butterfly.active = 0;
            scene.Step(Client::Input::InputState{});
            scene.Step(Client::Input::InputState{});
            ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        }
        ASSERT_EQ(target.frames.size(), 3u);
        EXPECT_TRUE(target.frames[0].presented);
        EXPECT_TRUE(target.frames[1].presented);
        EXPECT_TRUE(target.frames[2].presented);
        scene.Leave();
    }
}

TEST_P(CompleteWorldEntryTest, ARealWorldEntryRunsLogicBetweenCompleteFrames)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Client::Tests::FakeTextureStore textures;
    Client::Tests::FakeClock clock;
    Client::Models::ClothWorld cloth;
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection(world);
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    auto data = Client::Tables::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    Client::Assets::GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    Client::Protocol::FrontEnd::CharacterSelectResult reply;
    reply.map = GetParam();
    reply.x = 135;
    reply.y = 125;
    reply.pk = 3;
    world.OwnCharacter().ClearEquipmentForEntry();
    Publisher publisher;
    const Client::Tests::LetterMeasure letters;
    Client::Scenes::WorldEntryResources resources{world,
                                                  selection,
                                                  m_dataRoot,
                                                  cloth,
                                                  camera,
                                                  cameraInput,
                                                  models,
                                                  &data.Value().items->table,
                                                  &*data.Value().itemSets,
                                                  log,
                                                  &clock};
    // El renglón de bienvenida del F3 03 (0x4c4b60).
    resources.textMeasure = &letters;
    resources.texts = &*data.Value().text;
    resources.states = &states;
    const auto entered = Client::Scenes::ApplyWorldEntry(reply, 1, resources, publisher);
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    world.Runtime().drawingEnabled = true;
    Client::Input::InputState input;
    const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                               .textures = textures,
                                               .log = log,
                                               .clock = &clock,
                                               .cloth = &cloth,
                                               .input = &input,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .selection = &selection,
                                               .world = &world,
                                               .states = &states};
    Client::Scenes::GameScene scene(context, nullptr, &models, &data.Value().items->table, &models.TextureSlots(),
                                    &*data.Value().gates, &*data.Value().skills);
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    for (int frame = 0; frame < 4; ++frame)
    {
        scene.Step(input);
        ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        scene.PrepareFrame();
        auto& canvas = target.BeginFrame(clock.Now());
        scene.Render(canvas, 0.0f);
        ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
        target.Present();
        clock.Advance(std::chrono::milliseconds(47));
    }
    EXPECT_EQ(cloth.logicSteps, 4);
    ASSERT_EQ(target.frames.size(), 4u);
    for (const auto& frame : target.frames)
        EXPECT_TRUE(frame.presented);
    // La espera de un gate retiene el dibujo, pero sigue la lógica posterior a MoveHero.
    auto& arrival = world.Map().effects.Effects()[0];
    ASSERT_TRUE(arrival.alive);
    const auto life = arrival.life;
    world.Runtime().warpCounter = 100;
    scene.Step(input);
    ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
    EXPECT_EQ(world.Runtime().warpCounter, 99);
    EXPECT_EQ(arrival.life, life - 1);
    EXPECT_EQ(cloth.logicSteps, 5);
    scene.PrepareFrame();
    EXPECT_TRUE(scene.HoldsTheLastFrame());
    if (GetParam() == 0)
    {
        const auto hero = world.Runtime().hero;
        for (const std::uint8_t destination : {std::uint8_t{3}, std::uint8_t{0}})
        {
            SCOPED_TRACE(static_cast<int>(destination));
            const Client::Protocol::Movement::Teleport teleport{1, destination, 135, 125, 1};
            const auto changed = Client::Scenes::ApplyTeleport(teleport, 1, resources, publisher);
            ASSERT_TRUE(changed.HasValue()) << changed.Error();
            EXPECT_EQ(world.Runtime().hero, hero);
            EXPECT_EQ(world.Runtime().map, destination);
            EXPECT_EQ(world.Runtime().warpCounter, 30);
            clock.Advance(std::chrono::milliseconds(47));
            scene.Step(input);
            ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
            scene.PrepareFrame();
            EXPECT_FALSE(scene.HoldsTheLastFrame());
            auto& transitioned = target.BeginFrame(clock.Now());
            scene.Render(transitioned, 0.0f);
            ASSERT_FALSE(scene.Failure().has_value()) << *scene.Failure();
            target.Present();
        }
        EXPECT_EQ(publisher.confirmations, 2);
        EXPECT_EQ(target.frames.size(), 6u);
    }
    scene.Leave();
}
INSTANTIATE_TEST_SUITE_P(ReferenceMaps, CompleteWorldEntryTest,
                         ::testing::Values(std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}));
} // namespace
