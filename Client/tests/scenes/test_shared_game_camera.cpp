#include "App/PacingMode.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Input/InputState.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/GameData.h"
#include "Scenes/GameScene.h"
#include "Scenes/IScene.h"
#include "Scenes/LoginModels.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/TextureSets.h"
#include "World/GameCamera.h"
#include "World/Leaves.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"

#include "support/FakeTextureStore.h"
#include "support/FixedRandom.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SessionHarness.h"
#include "support/TemporaryFolder.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono> // IWYU pragma: keep — los literales de chrono_literals no los ve include-cleaner.
#include <initializer_list>
#include <memory>

namespace
{
using Client::World::GameCamera;
using Client::World::GameCameraInput;
using Client::World::StepGameCamera;
using namespace std::chrono_literals;

class SharedGameCameraTest : public testing::Test
{
protected:
    void SetUp() override
    {
        Client::Tests::WriteTextureSet(m_folder.Path(), Client::Scenes::LoginTextureSet);
        Client::Tests::WriteTextureSet(m_folder.Path(), Client::Scenes::SelectionTextureSet);
        Client::Tests::WriteTextureSet(m_folder.Path(), Client::Scenes::WindowTextureSet);
    }

    Client::Scenes::SceneContext Context(Client::App::PacingMode pacing = Client::App::PacingMode::Decoupled)
    {
        return {.dataRoot = m_folder.Path(),
                .textures = m_store,
                .log = m_log,
                .pacing = pacing,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput};
    }

    Client::Render::Pass3D Frame(Client::Scenes::IScene& scene, Client::Core::Duration clock)
    {
        auto& canvas = m_target.BeginFrame(clock);
        scene.Render(canvas, 0.0f);
        m_target.Present();
        const auto& events = m_target.frames.back().events;
        const auto models =
            std::ranges::find(events, Client::Tests::FrameEvent::Kind::Models, &Client::Tests::FrameEvent::kind);
        EXPECT_NE(models, events.end());
        if (models == events.end() || !models->pass)
            return {};
        return *models->pass;
    }

    Client::Tests::TemporaryFolder m_folder{"shared-camera"};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::RecordingFrameTarget m_target;
    Client::Scenes::SingleModelLibrary m_models;
    Client::Scenes::GameData m_data;
    GameCamera m_camera;
    GameCameraInput m_cameraInput;
};

TEST_F(SharedGameCameraTest, LoginAndSelectionDrawWithTheZoomLeftByTheGame)
{
    m_camera.zoom = 210.0f;
    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    EXPECT_EQ(m_camera.zoom, 210.0f);
    auto expected = Client::Scenes::LoginCamera();
    expected.position = {12636.5791015625f, 12819.4208984375f, 579.0f};
    EXPECT_EQ(Frame(login, 0ns).view,
              Client::Render::MakePass3D({640, 480}, Client::Scenes::WorldViewport, expected).view);
    EXPECT_EQ(m_camera.zoom, 200.0f);
    login.Leave();

    Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, nullptr);
    ASSERT_TRUE(selection.Enter().HasValue());
    EXPECT_EQ(m_camera.zoom, 200.0f);
    auto selected = Client::Scenes::SelectionCamera();
    selected.position = {13774.6494140625f, 14616.3505859375f, 543.0f};
    const auto selectedPass = Client::Render::MakePass3D({640, 480}, Client::Scenes::WorldViewport, selected);
    const auto drawn = Frame(selection, 0ns);
    EXPECT_EQ(drawn.view, selectedPass.view);
    EXPECT_EQ(drawn.projection, selectedPass.projection);
    EXPECT_EQ(drawn.farPlane, selectedPass.farPlane);
    EXPECT_EQ(m_camera.zoom, 190.0f);
    EXPECT_EQ(m_camera.yaw, -45.0f);
    selection.Leave();
}

TEST_F(SharedGameCameraTest, IntermediateDrawsAndLogicStepsDoNotConsumeCameraFrames)
{
    m_camera.zoom = 210.0f;
    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    const auto first = Frame(login, 0ns);
    for (int k = 0; k < 3; ++k)
        login.Step(Client::Input::InputState{});
    EXPECT_EQ(m_camera.zoom, 200.0f);
    EXPECT_EQ(Frame(login, 1ms).view, first.view);
    EXPECT_EQ(Frame(login, 40ms).view, first.view);
    EXPECT_EQ(m_camera.zoom, 200.0f);
    static_cast<void>(Frame(login, Client::Render::Models::OriginalFrameTime));
    EXPECT_EQ(m_camera.zoom, 190.0f);
    login.Leave();
    Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, nullptr);
    ASSERT_TRUE(selection.Enter().HasValue());
    const auto selected = Frame(selection, 0ns);
    EXPECT_EQ(m_camera.zoom, 180.0f);
    EXPECT_EQ(Frame(selection, 1ms).view, selected.view);
    EXPECT_EQ(m_camera.zoom, 180.0f);
    selection.Leave();
}

// ADR0005: el primer dibujo de cada entrada cuenta un frame; el carry de otra entrada no se recupera.
TEST_F(SharedGameCameraTest, DecoupledReentryStartsOneFrameAndPreservesTheCameraState)
{
    m_camera.zoom = 210.0f;
    m_camera.fixedHeight = 1201.0f;
    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    const auto first = Frame(login, 0ns);
    EXPECT_EQ(Frame(login, 40ms).view, first.view);
    EXPECT_EQ(m_camera.zoom, 200.0f);
    login.Leave();
    ASSERT_TRUE(login.Enter().HasValue());
    EXPECT_EQ(m_camera.zoom, 200.0f);
    EXPECT_EQ(m_camera.fixedHeight, 1201.0f);
    const auto next = Frame(login, 40ms);
    EXPECT_EQ(m_camera.zoom, 190.0f);
    EXPECT_EQ(Frame(login, 41ms).view, next.view);
    EXPECT_EQ(m_camera.zoom, 190.0f);
    login.Leave();
}

TEST_F(SharedGameCameraTest, FaithfulDrawsAdvanceEvenAtTheSameClockAndReentryKeepsTheState)
{
    m_camera.zoom = 210.0f;
    m_camera.fixedHeight = 1201.0f;
    Client::Scenes::LoginScene login(Context(Client::App::PacingMode::Faithful), m_data, m_models, nullptr, "1.0",
                                     Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    static_cast<void>(Frame(login, 0ns));
    static_cast<void>(Frame(login, 0ns));
    EXPECT_EQ(m_camera.zoom, 190.0f);
    login.Leave();
    ASSERT_TRUE(login.Enter().HasValue());
    EXPECT_EQ(m_camera.zoom, 190.0f);
    static_cast<void>(Frame(login, 0ns));
    EXPECT_EQ(m_camera.zoom, 180.0f);
    EXPECT_EQ(m_camera.fixedHeight, 1201.0f);
    login.Leave();
    Client::Scenes::CharacterSelectScene selection(Context(Client::App::PacingMode::Faithful), m_data, m_models,
                                                   nullptr);
    ASSERT_TRUE(selection.Enter().HasValue());
    static_cast<void>(Frame(selection, 0ns));
    static_cast<void>(Frame(selection, 0ns));
    EXPECT_EQ(m_camera.zoom, 160.0f);
    EXPECT_EQ(m_camera.fixedHeight, 1201.0f);
    selection.Leave();
}

TEST_F(SharedGameCameraTest, TheGameOnlyWritesTheYawInItsFirstLogicStep)
{
    m_camera.yaw = -40.0f;
    m_camera.distance = 3200.0f;
    m_camera.zoom = 210.0f;
    m_camera.fixedHeight = 1201.0f;
    Client::Scenes::GameScene game(Context(), nullptr);
    ASSERT_TRUE(game.Enter().HasValue());
    game.Step(Client::Input::InputState{});
    EXPECT_EQ(m_camera.yaw, -45.0f);
    EXPECT_EQ(m_camera.distance, 3200.0f);
    EXPECT_EQ(m_camera.zoom, 210.0f);
    EXPECT_EQ(m_camera.fixedHeight, 1201.0f);
    m_camera.yaw = -36.0f;
    game.Step(Client::Input::InputState{});
    EXPECT_EQ(m_camera.yaw, -36.0f);
}

TEST_F(SharedGameCameraTest, SelectionWaitsForTheListThenDrawsEvenWhenTheReceivedListIsEmpty)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    m_camera.zoom = 210.0f;
    m_camera.distance = 3200.0f;
    m_cameraInput.hero = {17500.0f, 20000.0f, 165.0f};
    Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, &online.session);
    ASSERT_TRUE(selection.Enter().HasValue());
    // La sesión está esperando F3/00: no hay consumo del carry/cámara todavía.
    for (auto clock : {0ns, std::chrono::duration_cast<Client::Core::Duration>(1s)})
    {
        selection.Render(m_target.BeginFrame(clock), 0.0f);
        m_target.Present();
        EXPECT_EQ(m_camera.zoom, 210.0f);
        EXPECT_EQ(m_camera.distance, 3200.0f);
        EXPECT_FALSE(m_camera.lastFrameDistance.has_value());
        EXPECT_EQ(m_cameraInput.hero, (glm::vec3{17500.0f, 20000.0f, 165.0f}));
    }
    online.ReceiveTheList({});
    selection.Step(Client::Input::InputState{});
    EXPECT_EQ(m_cameraInput.hero.z, 165.0f);
    // El primer dibujo llega más tarde que la última espera, sin recuperar ese tiempo.
    EXPECT_EQ(Frame(selection, 2s).farPlane, 3000.0f);
    EXPECT_EQ(m_camera.zoom, 200.0f);
    EXPECT_NE(m_camera.distance, 3200.0f);
    EXPECT_EQ(m_cameraInput.hero, (glm::vec3{17500.0f, 20000.0f, Client::Scenes::CharacterSlotHeight}));
    selection.Leave();
}

TEST_F(SharedGameCameraTest, ReceivingSlotZeroWritesTheSharedHeroXYBeforeTheCameraDraw)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    m_cameraInput.hero = {17500.0f, 20000.0f, 165.0f};
    Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, &online.session);
    ASSERT_TRUE(selection.Enter().HasValue());
    online.ReceiveTheList({{0, "char0001"}});
    selection.Step(Client::Input::InputState{});
    // 0x5172c4/0x5172ca: las escrituras de 0x517270 al slot0, al que apunta Hero.
    EXPECT_EQ(m_cameraInput.hero.x, Client::Scenes::CharacterSlots[0].position.x);
    EXPECT_EQ(m_cameraInput.hero.y, Client::Scenes::CharacterSlots[0].position.y);
    EXPECT_EQ(Frame(selection, 0ns).farPlane, Client::Scenes::SelectionCamera().farPlane);
    selection.Leave();
}

TEST_F(SharedGameCameraTest, TheSlotZeroSelectionPassDoesNotDependOnInitialHeroZ)
{
    const auto expected =
        Client::Render::MakePass3D({640, 480}, Client::Scenes::WorldViewport, Client::Scenes::SelectionCamera());
    // Z sintéticas para aislar el cálculo de cámara; ninguna representa la Z original del actor.
    for (float initialZ : {-10000.0f, 1000000.0f})
    {
        m_camera = {};
        m_cameraInput.hero = {17500.0f, 20000.0f, initialZ};
        Client::Tests::LoggedInSession online;
        ASSERT_NO_FATAL_FAILURE(online.LogIn());
        Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, &online.session);
        ASSERT_TRUE(selection.Enter().HasValue());
        online.ReceiveTheList({{0, "char0001"}});
        selection.Step(Client::Input::InputState{});
        const auto drawn = Frame(selection, 0ns);
        EXPECT_EQ(drawn.view, expected.view);
        EXPECT_EQ(drawn.projection, expected.projection);
        EXPECT_EQ(drawn.farPlane, expected.farPlane);
        selection.Leave();
    }
}

TEST_F(SharedGameCameraTest, ReenteringWithTheSameSlotZeroRefreshesTheSharedHeroXY)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    Client::Scenes::CharacterSelectScene selection(Context(), m_data, m_models, &online.session);
    ASSERT_TRUE(selection.Enter().HasValue());
    online.ReceiveTheList({{0, "char0001"}});
    selection.Step(Client::Input::InputState{});
    static_cast<void>(Frame(selection, 0ns));
    selection.Leave();

    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    static_cast<void>(Frame(login, 0ns));
    EXPECT_EQ(m_cameraInput.hero, Client::Scenes::LoginHeroPosition);
    login.Leave();

    // La lista autoritativa no cambió; se reutiliza la instancia para cubrir también SameEntry.
    ASSERT_TRUE(selection.Enter().HasValue());
    selection.Step(Client::Input::InputState{});
    EXPECT_EQ(m_cameraInput.hero.x, Client::Scenes::CharacterSlots[0].position.x);
    EXPECT_EQ(m_cameraInput.hero.y, Client::Scenes::CharacterSlots[0].position.y);
    selection.Leave();
}

TEST_F(SharedGameCameraTest, LoginPreservesTheHeroUntilItsFirstDrawAndOnlyResetsYawOnEntry)
{
    m_cameraInput.hero = {17500.0f, 20000.0f, 165.0f};
    m_camera.yaw = -40.0f;
    m_camera.distance = 3200.0f;
    m_camera.zoom = 210.0f;
    m_camera.fixedHeight = 1201.0f;
    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    EXPECT_EQ(m_cameraInput.hero, (glm::vec3{17500.0f, 20000.0f, 165.0f}));
    EXPECT_EQ(m_camera.yaw, 0.0f);
    EXPECT_EQ(m_camera.distance, 3200.0f);
    EXPECT_EQ(m_camera.zoom, 210.0f);
    EXPECT_EQ(m_camera.fixedHeight, 1201.0f);
    EXPECT_EQ(Frame(login, 0ns).farPlane, 20400.0f);
    EXPECT_EQ(m_cameraInput.hero, Client::Scenes::LoginHeroPosition);
    login.Leave();
}

TEST_F(SharedGameCameraTest, TheFixedHeightIsSharedWithHeightQueriesAndTerrainVertices)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    attributes->cells.fill(0x40);
    mapping->layer1.fill(0);
    mapping->layer2.fill(0xff);
    Client::World::TerrainView view;
    view.heights = heights.get();
    view.attributes = attributes.get();
    view.mapping = mapping.get();
    view.lighting = lighting.get();
    const auto check = [&](float expected)
    {
        EXPECT_EQ(Client::World::TerrainHeightAt(*heights, *attributes, 100.0f, 100.0f, m_camera.fixedHeight),
                  expected);
        EXPECT_EQ(Client::World::TerrainHeightAt(*heights, *attributes, 100.0f, 30000.0f, m_camera.fixedHeight),
                  expected);
        view.fixedHeight = m_camera.fixedHeight;
        Client::Render::ModelBatch batch;
        batch.Begin({});
        Client::World::DrawTerrainCell(batch, view, {}, 10, 10);
        ASSERT_FALSE(batch.Vertices().empty());
        for (const auto& vertex : batch.Vertices())
            EXPECT_EQ(vertex.position.z, expected);
    };
    check(1200.0f);
    GameCameraInput input;
    input.cellAttribute = 0x40;
    static_cast<void>(StepGameCamera(m_camera, input));
    check(1201.0f);
    input.cellAttribute = 0;
    input.scene = Client::World::CullingScene::Login;
    input.map = 55;
    static_cast<void>(StepGameCamera(m_camera, input));
    input.scene = Client::World::CullingScene::CharacterSelect;
    input.map = 54;
    static_cast<void>(StepGameCamera(m_camera, input));
    check(1201.0f);
}
// D grande permite observar el tope de 64; con D normal ya habría convergido antes.
TEST_F(SharedGameCameraTest, ALongDrawUsesTheExistingOriginalFrameLimit)
{
    m_camera.distance = 0x1p100f;
    GameCamera expected = m_camera;
    GameCameraInput input;
    input.scene = Client::World::CullingScene::Login;
    input.map = 55;
    input.hero = Client::Scenes::LoginHeroPosition;
    for (int frame = 0; frame < 1 + Client::Render::Models::MaxOriginalFramesPerDraw; ++frame)
        static_cast<void>(StepGameCamera(expected, input));
    Client::Scenes::LoginScene login(Context(), m_data, m_models, nullptr, "1.0", Client::Tests::FixedSeed);
    ASSERT_TRUE(login.Enter().HasValue());
    static_cast<void>(Frame(login, 0ns));
    static_cast<void>(Frame(login, 100s));
    EXPECT_EQ(m_camera.distance, expected.distance);
    login.Leave();
}

TEST_F(SharedGameCameraTest, TheEmbersUseTheCurrentSharedGroundHeight)
{
    auto heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    auto attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    attributes->cells.fill(0x40);
    Client::World::Leaves leaves;
    for (auto& leaf : leaves.All())
    {
        leaf.alive = true;
        leaf.position = {100.0f, 100.0f, 1200.5f};
        leaf.light = {1.0f, 1.0f, 1.0f};
    }
    Client::World::EmberWorld world;
    world.heights = heights.get();
    world.attributes = attributes.get();
    world.fixedHeight = 1201.0f;
    Client::Render::Models::CrtRandom random{1};
    Client::World::StepLoginEmbers(leaves, world, random);
    EXPECT_EQ(leaves.All()[0].position.z, 1201.0f);
    EXPECT_EQ(leaves.All()[0].light.x, 0.95f);
}

} // namespace
