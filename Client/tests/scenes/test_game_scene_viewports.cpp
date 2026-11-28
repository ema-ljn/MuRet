#include "Scenes/GameScene.h"

#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Render/ModelVertex.h"
#include "Models/Cloth.h"
#include "Math/RoundedTransforms.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Session/IWorldEntryPublisher.h"
#include "Tables/GameData.h"
#include "Assets/GameModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainPickingSegment.h"

#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/LetterMeasure.h"
#include "support/PlayerActors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/trigonometric.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// T15, bounded CPU coverage: real F3 03, Data terrain and 0/1/3 logical steps before the first draw. The real
// resources let it present the full frame and then an intermediate draw without advancing the per-original-frame
// producers again. RecordingFrameTarget watches BeginFrame/Present and the batches, with no GL or visual/VM comparison.
namespace
{
constexpr float SelectionFar = std::bit_cast<float>(std::uint32_t{0x4633ff9a});

struct ViewportProbe
{
    Client::Core::PixelSize screen;
    glm::ivec4 viewport;
    float mouseX;
    float mouseY;
    float projectionX;
    glm::vec3 rayDelta;
    std::string_view name;
};

// 0x620db0: height 432/480 and integer division. The 768 leaves 691 world pixels and 77 of strip.
// 0x620480: analytic rays for pitch -48.5°, yaw -45°, fov 35° and far 2000, after the mouse's two truncations
// (WndProc and back to pixels). They do not use MakePass3D/RayThrough as the oracle.
constexpr std::array<ViewportProbe, 3> Viewports{{
    {{640, 480}, {0, 48, 640, 432}, 481.75f, 108.75f, 2.14082649f, {-890.96847f, 1555.68950f, -1062.85695f}, "640x480"},
    {{1024, 768},
     {0, 77, 1024, 691},
     770.8f,
     174.0f,
     2.14020704f,
     {-892.40699f, 1555.77170f, -1061.64153f},
     "1024x768"},
    {{1920, 1080},
     {0, 108, 1920, 972},
     1445.25f,
     244.6875f,
     1.60561987f,
     {-780.18163f, 1666.47634f, -1062.85695f},
     "1920x1080"},
}};

struct EntryProbe
{
    std::uint8_t map;
    std::uint8_t x;
    std::uint8_t y;
    bool selectedSeen;
    std::string_view name;
};

constexpr std::array<EntryProbe, 2> Entries{{{0, 133, 127, true, "Lorencia"}, {2, 220, 30, false, "Devias"}}};
using ViewportCase = std::tuple<ViewportProbe, int, EntryProbe>;

// The selection's 0x5fb570: W640, F=13823.88, T=6497.223..., 45° turn and centre (12975, 15423, 333). Fixture of
// the last draw: the entry and the steps must not replace it.
constexpr Client::Terrain::TerrainPolygon SelectedPolygon{
    .x = {29.475204467773438f, 126.41022491455078f, 179.11355590820312f, 135.126220703125f},
    .y = {157.5697784423828f, 254.5048065185547f, 148.853759765625f, 104.866455078125f}};

class EntryPublisher final : public Client::Scenes::IWorldEntryPublisher
{
public:
    void PublishWorldEntry() override
    {
        ++calls;
    }

    int calls = 0;
};

class GameSceneViewportTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<ViewportCase>
{
protected:
    void EnterWorld()
    {
        const auto& [viewport, steps, entry] = GetParam();
        static_cast<void>(steps);
        m_target.SetScreen(viewport.screen);
        m_input.Apply({.kind = Client::Input::InputEvent::Kind::Motion, .x = viewport.mouseX, .y = viewport.mouseY});
        auto loadedData = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loadedData.HasValue()) << loadedData.Error();
        m_data = std::move(loadedData).Value();
        const auto loadedModels = m_models.Load(m_dataRoot, m_store, m_log);
        ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
        m_world.OwnCharacter().ClearEquipmentForEntry();
        m_states.camera.trapezoid = SelectedPolygon;
        m_camera.farPlane = SelectionFar;
        m_camera.lastFrameDistance = 1000.0f;
        Client::Protocol::FrontEnd::CharacterSelectResult reply;
        reply.map = entry.map;
        reply.x = entry.x;
        reply.y = entry.y;
        reply.pk = 3;
        const Client::Scenes::WorldEntryResources resources{.world = m_world,
                                                            .selection = m_selection,
                                                            .dataRoot = m_dataRoot,
                                                            .cloth = m_cloth,
                                                            .camera = m_camera,
                                                            .cameraInput = m_cameraInput,
                                                            .models = m_models,
                                                            .items = &m_data.items->table,
                                                            .itemSetTables = &*m_data.itemSets,
                                                            .log = m_log,
                                                            .textMeasure = &m_letters,
                                                            .texts = &*m_data.text,
                                                            .states = &m_states};
        EntryPublisher publisher;
        const auto entered = Client::Scenes::ApplyWorldEntry(reply, 5, resources, publisher);
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        ASSERT_EQ(publisher.calls, 1);
        ASSERT_EQ(m_world.Runtime().hero, static_cast<std::size_t>(m_world.Runtime().heroSlot));
        // The actor comes from 0x511290/0x512a30; the double only provides Player.bmd's 205 actions.
        ASSERT_EQ(Hero().type, Client::Tables::PlayerSkeletonModel);
        ASSERT_EQ(Hero().word376, 0xffff);
        ASSERT_TRUE(Hero().alive);
        EXPECT_EQ(Hero().position.x, static_cast<float>(entry.x) * 100.0f + 50.0f);
        EXPECT_EQ(Hero().position.y, static_cast<float>(entry.y) * 100.0f + 50.0f);
        EXPECT_EQ(m_world.Runtime().warpCounter, 30);
        ExpectSelectedPolygon();
        // The selection already left 0x7d3a101 at 1; F3 03 does not write it.
        m_world.Runtime().drawingEnabled = true;
    }

    void StartGame()
    {
        m_manager = std::make_unique<Client::Scenes::SceneManager>(
            [this](Client::Kernel::SceneId /*id*/)
            {
                const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                           .textures = m_store,
                                                           .log = m_log,
                                                           .clock = &m_clock,
                                                           .cloth = &m_cloth,
                                                           .input = &m_input,
                                                           .camera = &m_camera,
                                                           .cameraInput = &m_cameraInput,
                                                           .selection = &m_selection,
                                                           .world = &m_world,
                                                           .states = &m_states};
                auto game = std::make_unique<Client::Scenes::GameScene>(context, nullptr, &m_models,
                                                                        &m_data.items->table, &m_models.TextureSlots(),
                                                                        &*m_data.gates, &*m_data.skills, &*m_data.text);
                m_game = game.get();
                return game;
            },
            m_target, m_clock, nullptr, m_log);
        const auto started = m_manager->Start(Client::Kernel::SceneId::Game);
        ASSERT_TRUE(started.HasValue()) << started.Error();
    }

    void RunSteps(int count)
    {
        for (int step = 0; step < count; ++step)
        {
            m_clock.Advance(std::chrono::milliseconds(40));
            m_manager->Step(m_input);
            ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        }
    }

    [[nodiscard]] Client::Kernel::ActorState& Hero()
    {
        return m_world.Hero();
    }

    void ExpectSelectedPolygon() const
    {
        EXPECT_EQ(m_states.camera.trapezoid.x, SelectedPolygon.x);
        EXPECT_EQ(m_states.camera.trapezoid.y, SelectedPolygon.y);
        EXPECT_FALSE(m_states.camera.trapezoid.everything);
        EXPECT_EQ(m_states.camera.trapezoidLevel.effectiveLevel, 0);
    }

    void ExpectGamePolygon() const
    {
        // The game's 0x5fb570: F/T/N=2400/1128/456, half widths 1190/540 and 45° turn.
        constexpr std::array<float, 4> OffsetX{-17.40896797f, -0.57982755f, 8.57013416f, 0.93338096f};
        constexpr std::array<float, 4> OffsetY{0.57982755f, 17.40896797f, -0.93338096f, -8.57013416f};
        const EntryProbe& entry = std::get<2>(GetParam());
        const auto& polygon = m_states.camera.trapezoid;
        for (std::size_t corner = 0; corner < OffsetX.size(); ++corner)
        {
            SCOPED_TRACE(corner);
            EXPECT_NEAR(polygon.x[corner], static_cast<float>(entry.x) + 0.5f + OffsetX[corner], 0.00005f);
            EXPECT_NEAR(polygon.y[corner], static_cast<float>(entry.y) + 0.5f + OffsetY[corner], 0.00005f);
        }
        EXPECT_FALSE(polygon.everything);
        EXPECT_EQ(m_states.camera.trapezoidLevel.effectiveLevel, 0);
    }

    void ExpectCompleteFrame() const
    {
        ASSERT_FALSE(m_manager->Failure().has_value()) << *m_manager->Failure();
        ASSERT_FALSE(m_game->Failure().has_value()) << *m_game->Failure();
        EXPECT_FALSE(m_manager->HoldsTheLastFrame());
    }

    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Tests::FakeTextureStore m_store;
    Client::Tests::FakeClock m_clock;
    Client::Input::InputState m_input;
    Client::Models::ClothWorld m_cloth{.random = Client::Crt::CrtRandom(0x1234)};
    Client::Kernel::WorldState m_world;
    Client::Session::ModuleStates m_states;
    Client::FrontEnd::SelectionState m_selection{m_world};
    Client::Camera::GameCamera m_camera;
    Client::Camera::GameCameraInput m_cameraInput;
    Client::Assets::GameModels m_models;
    Client::Tables::GameData m_data;
    // The F3 03 welcome line (0x4c4b60).
    Client::Tests::LetterMeasure m_letters;
    Client::Tests::RecordingFrameTarget m_target;
    Client::Scenes::GameScene* m_game = nullptr;
    std::unique_ptr<Client::Scenes::SceneManager> m_manager;
};

TEST_P(GameSceneViewportTest, TheFirstGameDrawKeepsTheNativeViewportCameraRayAndTrapezoid)
{
    const auto& [viewport, steps, entry] = GetParam();
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_NO_FATAL_FAILURE(RunSteps(steps));
    EXPECT_EQ(m_cloth.logicSteps, steps);
    EXPECT_EQ(m_world.Runtime().warpCounter, 30 - steps);
    EXPECT_EQ(Hero().seen, steps > 0 && entry.selectedSeen);
    EXPECT_FLOAT_EQ(m_camera.farPlane, SelectionFar);
    ExpectSelectedPolygon();
    ASSERT_FALSE(m_manager->HoldsTheLastFrame());

    m_manager->Render(0.5f);

    ASSERT_NO_FATAL_FAILURE(ExpectCompleteFrame());
    ASSERT_EQ(m_target.frames.size(), 1u);
    const Client::Tests::RecordedFrame& frame = m_target.frames.front();
    EXPECT_TRUE(frame.presented);
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : frame.events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            // The new tail flushes several batches; the first keeps the terrain pass.
            if (models == nullptr)
            {
                models = &event;
            }
        }
    }
    ASSERT_NE(models, nullptr);
    ASSERT_TRUE(models->pass.has_value());
    EXPECT_FALSE(models->modelVertices.empty());
    const Client::Render::Pass3D& pass = *models->pass;
    EXPECT_EQ(pass.viewport, viewport.viewport);
    EXPECT_EQ(pass.screen.width, viewport.screen.width);
    EXPECT_EQ(pass.screen.height, viewport.screen.height);
    EXPECT_FLOAT_EQ(pass.farPlane, 2000.0f);
    EXPECT_NEAR(pass.projection[0][0], viewport.projectionX, 0.000001f);
    EXPECT_NEAR(pass.projection[1][1], 3.17159480f, 0.000001f);
    EXPECT_EQ(m_cameraInput.hero, Hero().position);
    EXPECT_EQ(m_cameraInput.map, entry.map);
    EXPECT_FLOAT_EQ(m_camera.distance, 1000.0f);
    EXPECT_FLOAT_EQ(m_camera.targetDistance, 1000.0f);
    EXPECT_FLOAT_EQ(m_camera.yaw, -45.0f);
    EXPECT_FLOAT_EQ(m_camera.farPlane, 2000.0f);
    ASSERT_TRUE(m_camera.lastFrameDistance.has_value());
    EXPECT_FLOAT_EQ(*m_camera.lastFrameDistance, 1000.0f);
    ASSERT_TRUE(m_game->MouseRay().has_value());
    const Client::Terrain::TerrainPickingSegment ray = *m_game->MouseRay();
    // 0x635ae0: D=1000, offset (D/√2, -D/√2), height HeroZ+D-150.
    const glm::vec3 cameraOffset = ray.origin - Hero().position;
    EXPECT_NEAR(cameraOffset.x, 707.10678f, 0.003f);
    EXPECT_NEAR(cameraOffset.y, -707.10678f, 0.003f);
    EXPECT_NEAR(cameraOffset.z, 850.0f, 0.003f);
    const glm::vec3 delta = ray.end - ray.origin;
    EXPECT_NEAR(delta.x, viewport.rayDelta.x, 0.01f);
    EXPECT_NEAR(delta.y, viewport.rayDelta.y, 0.01f);
    EXPECT_NEAR(delta.z, viewport.rayDelta.z, 0.01f);
    ExpectGamePolygon();

    // An intermediate draw keeps the camera/ray oracles and only closes the wait read.
    const std::uint32_t random = m_cloth.random.State();
    m_manager->Render(0.5f);
    ASSERT_NO_FATAL_FAILURE(ExpectCompleteFrame());
    ASSERT_EQ(m_target.frames.size(), 2u);
    EXPECT_TRUE(m_target.frames.back().presented);
    EXPECT_EQ(m_cloth.random.State(), Client::Tests::AfterDraws(random, 4).State());
    EXPECT_EQ(m_cloth.logicSteps, steps);
    EXPECT_EQ(m_game->MouseRay()->origin, ray.origin);
    EXPECT_EQ(m_game->MouseRay()->end, ray.end);
    ExpectGamePolygon();
}

// 0x63733e and 0x63738a: with the inventory open, 0x5bb8d0 gives 450 and the world pass goes with that width
// (450 · W / 640 pixels, with its aspect), in Lorencia and Devias; the frame goes on to the HUD, which draws the
// window.
TEST_P(GameSceneViewportTest, TheInventoryNarrowsTheWorldPass)
{
    const auto& [viewport, steps, entry] = GetParam();
    static_cast<void>(entry);
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_NO_FATAL_FAILURE(RunSteps(steps));
    m_world.Runtime().panels.inventory = 1;

    m_manager->Render(0.5f);

    ASSERT_NO_FATAL_FAILURE(ExpectCompleteFrame());
    ASSERT_EQ(m_target.frames.size(), 1u);
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : m_target.frames.front().events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models && models == nullptr)
            models = &event;
    }
    ASSERT_NE(models, nullptr);
    ASSERT_TRUE(models->pass.has_value());
    const Client::Render::Pass3D& pass = *models->pass;
    const int width = 450 * viewport.screen.width / 640;
    EXPECT_EQ(pass.viewport, glm::ivec4(0, viewport.viewport.y, width, viewport.viewport.w));
    EXPECT_NEAR(pass.projection[1][1], 3.17159480f, 0.000001f);
    EXPECT_NEAR(pass.projection[0][0],
                3.17159480f * static_cast<float>(viewport.viewport.w) / static_cast<float>(width), 0.00001f);
}

// 0x5ac120 with Item.bmd's models (P-C70): with the inventory open, the weapon of slot 0 and the shield of the first
// cell go in their own model batch, after the world's and a 2D close, with the full screen, the 1° perspective with
// 20 and 2000 and the view at identity; the vertices end up in front of the UI camera, 200 from it (0.1 of the way to
// −2000).
TEST_P(GameSceneViewportTest, TheInventoryItemsGoInTheUiPassAfterTheWorld)
{
    const auto& [viewport, steps, entry] = GetParam();
    static_cast<void>(entry);
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    ASSERT_NO_FATAL_FAILURE(StartGame());
    ASSERT_NO_FATAL_FAILURE(RunSteps(steps));
    m_world.Runtime().panels.inventory = 1;
    Client::Kernel::ItemState& weapon = m_world.OwnCharacter().equipment[0];
    weapon.id = 0x0000;
    weapon.occupied = 1;
    Client::Kernel::ItemState& shield = m_world.Inventory().cells[0];
    shield.id = 0x0c00;
    shield.occupied = 1;

    m_manager->Render(0.5f);

    ASSERT_NO_FATAL_FAILURE(ExpectCompleteFrame());
    ASSERT_EQ(m_target.frames.size(), 1u);
    const std::vector<Client::Tests::FrameEvent>& events = m_target.frames.front().events;
    std::vector<std::size_t> models;
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        if (events[index].kind == Client::Tests::FrameEvent::Kind::Models)
            models.push_back(index);
    }
    ASSERT_GE(models.size(), 2u);
    const std::size_t ui = models.back();
    ASSERT_GT(ui, 0u);
    EXPECT_EQ(events[ui - 1].kind, Client::Tests::FrameEvent::Kind::Flush);
    ASSERT_TRUE(events[ui].pass.has_value());
    const Client::Render::Pass3D& pass = *events[ui].pass;
    EXPECT_EQ(pass.viewport, glm::ivec4(0, 0, viewport.screen.width, viewport.screen.height));
    const float aspect = static_cast<float>(static_cast<double>(viewport.screen.width) / viewport.screen.height);
    EXPECT_EQ(pass.projection, Client::Math::RoundedPerspective(glm::radians(1.0f), aspect, 20.0f, 2000.0f));
    EXPECT_EQ(pass.view, glm::mat4(1.0f));
    ASSERT_FALSE(events[ui].modelVertices.empty());
    for (const Client::Render::ModelVertex& vertex : events[ui].modelVertices)
    {
        EXPECT_GT(vertex.position.z, -205.0f);
        EXPECT_LT(vertex.position.z, -195.0f);
    }
}

TEST_P(GameSceneViewportTest, TheWaitRetainsThePresentedFrameUntilTheCounterReachesThirty)
{
    const auto& [viewport, steps, entry] = GetParam();
    static_cast<void>(viewport);
    static_cast<void>(entry);
    ASSERT_NO_FATAL_FAILURE(EnterWorld());
    m_world.Runtime().warpCounter = 31 + steps;
    ASSERT_NO_FATAL_FAILURE(StartGame());
    // The image already presented belongs to the previous frame; the held game does not start another.
    static_cast<void>(m_target.BeginFrame(m_clock.Now()));
    m_target.Present();
    ASSERT_NO_FATAL_FAILURE(RunSteps(steps));
    EXPECT_EQ(m_world.Runtime().warpCounter, 31);
    EXPECT_EQ(m_cloth.logicSteps, steps);
    EXPECT_TRUE(m_manager->HoldsTheLastFrame());
    const std::uint32_t random = m_cloth.random.State();
    EXPECT_TRUE(m_manager->HoldsTheLastFrame());
    EXPECT_EQ(m_cloth.random.State(), random) << "PrepareFrame se consulta una vez por frame";
    ASSERT_EQ(m_target.frames.size(), 1u);
    EXPECT_TRUE(m_target.frames.front().presented);
    EXPECT_FALSE(m_game->MouseRay().has_value());
    EXPECT_FLOAT_EQ(m_camera.farPlane, SelectionFar);
    ExpectSelectedPolygon();

    ASSERT_NO_FATAL_FAILURE(RunSteps(1));
    EXPECT_EQ(m_world.Runtime().warpCounter, 30);
    EXPECT_EQ(m_cloth.logicSteps, steps + 1);
    ASSERT_FALSE(m_manager->HoldsTheLastFrame());
    m_manager->Render(0.0f);

    ASSERT_NO_FATAL_FAILURE(ExpectCompleteFrame());
    ASSERT_EQ(m_target.frames.size(), 2u);
    EXPECT_TRUE(m_target.frames.front().presented);
    EXPECT_TRUE(m_target.frames.back().presented);
    EXPECT_TRUE(m_game->MouseRay().has_value());
    ExpectGamePolygon();
}

std::string ViewportCaseName(const ::testing::TestParamInfo<ViewportCase>& parameter)
{
    const auto& [viewport, steps, entry] = parameter.param;
    return std::string(entry.name) + "_" + std::string(viewport.name) + "_Steps" + std::to_string(steps);
}

INSTANTIATE_TEST_SUITE_P(ReferenceMaps, GameSceneViewportTest,
                         ::testing::Combine(::testing::ValuesIn(Viewports), ::testing::Values(0, 1, 3),
                                            ::testing::ValuesIn(Entries)),
                         ViewportCaseName);
} // namespace
