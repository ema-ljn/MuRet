#include "Scenes/GameScene.h"

#include "Session/CharacterEvent.h"
#include "App/GameLoop.h"
#include "Kernel/PacingMode.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Platform/WindowState.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/InputState.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Tables/GameData.h"
#include "Fauna/FaunaState.h"
#include "Assets/GameModels.h"
#include "Scenes/IScene.h"
#include "Camera/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "FrontEnd/SelectionState.h"
#include "Scenes/WorldLoadingScene.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Weather/Leaves.h"
#include "Effects/ModelEffects.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/WorldTerrain.h"
#include "Session/ModuleStates.h"

#include "support/CharacterPackets.h"
#include "support/GoldenDir.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/SessionHarness.h"
#include "Weather/LeafWeather.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace
{
class InitialGameWaitTest : public Client::Tests::GoldenDataTest
{
};

void Advance(Client::Crt::CrtRandom& random, int count)
{
    for (int index = 0; index < count; ++index)
        static_cast<void>(random.Next());
}

// 0x633f20 leaves World54 in SelectionState; 0x6356f0 presents the mosaic and 0x636290 starts waiting for F303
// without changing it. MoveHero's early return does not end the scene step.
TEST_F(InitialGameWaitTest, TheFirstGameStepKeepsSelectionTerrainAndStepsActorsEffectsAndClothBeforeF303)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    Client::Tests::CharacterEntryFields entry;
    entry.equipment[4] = 3; // Pet NONE, no helper: contractual list on the fake GS's real wire.
    online.ReceiveTheList({entry});
    ASSERT_TRUE(online.session.Characters().has_value());
    online.session.ChooseCharacter(std::uint8_t{0});

    Client::Tests::FakeTextureStore textures;
    Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom(0x1234)};
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection(world);
    Client::Session::ModuleStates states;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    auto data = Client::Tables::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    Client::Assets::GameModels models;
    const auto loadedModels = models.Load(m_dataRoot, textures, online.log);
    ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
    const auto* player = models.SkeletonOf(Client::Tables::PlayerSkeletonModelId);
    ASSERT_NE(player, nullptr);

    // The list's constructor reads the previous login's terrain; InitializeSelection then loads the real 54. No
    // synthetic skeleton or empty game TerrainWall is lent.
    auto loginTerrain = Client::Terrain::LoadWorldTerrain(m_dataRoot, 55);
    ASSERT_TRUE(loginTerrain.HasValue()) << loginTerrain.Error();
    selection.RetainTerrain(55, std::move(loginTerrain).Value());
    selection.BeginSelection();
    const std::array<Client::App::CharacterEvent, 1> events{
        Client::App::CharacterListReceived{*online.session.Characters()}};
    const auto applied = selection.Apply(events, camera, cameraInput, &cloth, player);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const auto initialized = selection.InitializeSelection(m_dataRoot, states.terrain, cameraInput);
    ASSERT_TRUE(initialized.HasValue()) << initialized.Error();
    selection.DrawSelection(cameraInput);
    states.camera.trapezoid = Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::CharacterSelect, 54,
                                                                Client::Camera::SelectionCullingPoint,
                                                                Client::Camera::SelectionCamera().angles.z);
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
    world.PrepareSelectedActor(*online.session.ChosenCharacter(), cloth.random);
    const auto polygon = states.camera.trapezoid;
    const auto* selectionTerrain = selection.Terrain();
    ASSERT_NE(selectionTerrain, nullptr);
    ASSERT_NE(selectionTerrain->heights, nullptr);
    ASSERT_NE(selectionTerrain->attributes, nullptr);
    // 0x633f4e loads the 54 into the world globals (0x61a560): the selection lends that terrain.
    ASSERT_EQ(selectionTerrain, &states.terrain.current);
    ASSERT_EQ(world.Runtime().map, 54);
    ASSERT_TRUE(world.Runtime().drawingEnabled);
    auto& hero = world.Hero();
    ASSERT_TRUE(hero.alive);
    ASSERT_EQ(hero.type, Client::Tables::PlayerSkeletonModel);
    const auto revision = hero.revision;
    const auto position = hero.position;
    const auto animation = hero.animation;
    const auto cellBefore = selectionTerrain->attributes->cells[0];
    const auto heightBefore = selectionTerrain->heights->cells[0];

    // Contractual probe of the live tail: Arrival uses its native creator with no made-up BMD or animation. It does not
    // claim that F303 arrived or that it creates this effect during the wait.
    const auto effectSlot = world.Map().effects.Create(Client::Effects::ArrivalEffect, position, {}, {1, 1, 1}, 1);
    ASSERT_TRUE(effectSlot.has_value());
    auto& effect = world.Map().effects.Effects()[*effectSlot];
    ASSERT_EQ(effect.life, 20);

    Client::Input::InputState input;
    const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                               .textures = textures,
                                               .log = online.log,
                                               .clock = &online.clock,
                                               .cloth = &cloth,
                                               .input = &input,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .selection = &selection,
                                               .world = &world,
                                               .states = &states};
    Client::Scenes::GameScene* game = nullptr;
    Client::Tests::RecordingFrameTarget target;
    Client::Scenes::SceneManager manager(
        [&](Client::Kernel::SceneId scene) -> std::unique_ptr<Client::Scenes::IScene>
        {
            if (scene == Client::Kernel::SceneId::WorldLoading)
                return std::make_unique<Client::Scenes::WorldLoadingScene>(context);
            auto created = std::make_unique<Client::Scenes::GameScene>(
                context, &online.session, &models, &data.Value().items->table, &models.TextureSlots(),
                &*data.Value().gates, &*data.Value().skills);
            game = created.get();
            return created;
        },
        target, online.clock, nullptr, online.log);
    const auto started = manager.Start(Client::Kernel::SceneId::WorldLoading);
    ASSERT_TRUE(started.HasValue()) << started.Error();
    manager.Render(0);
    ASSERT_EQ(manager.Failure(), std::nullopt);
    ASSERT_EQ(target.frames.size(), 1u);
    ASSERT_TRUE(target.frames.front().presented);
    const auto mosaicEvents = target.frames.front().events.size();
    manager.Step(input); // Scene 3's step goes to 5 after presenting the mosaic.
    ASSERT_EQ(manager.Current(), Client::Kernel::SceneId::Game);
    ASSERT_NE(game, nullptr);
    ASSERT_EQ(world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait);
    const auto before = cloth;
    Client::Models::ClothWorld expected = before;
    // 0x447108..0x4471b2: 54 admits the fallback of 0x425070 only with attribute 0/2. The earlier CRT decides the
    // candidates and their admission against the real .att, before GameScene is observed. The BSS slot keeps type/speed
    // 0: if admitted it takes timer/scale and one FullMovement rand; this fixture requires timer>1, with no expiry
    // draw. It creates no artificial type or BMD.
    constexpr float Reciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
    // 0x4eb620, at the frame start: 0x552f10 opens and closes the own record (the skill wait). 0x636920: the first
    // step's slide (0x46b010 and 0x46b0e0, which opens and closes the own record; without Slide.bmd it makes no random
    // pick). 0x636ded..0x63705b: the step's UI with everything closed (the skill bar, Q/W/E and IsOpen(0)), before the
    // objects and the fauna.
    Advance(expected.random, Client::Models::OwnRecordCloseRandoms);
    Advance(expected.random, Client::Models::OwnRecordCloseRandoms);
    Advance(expected.random, Client::Models::OwnRecordCloseRandoms + 2 + 2);
    int admittedFish = 0;
    for (int slot = 0; slot < 3; ++slot)
    {
        const float x = static_cast<float>(static_cast<double>(expected.random.Next() % 1024 - 512) + position.x);
        const float y = static_cast<float>(static_cast<double>(expected.random.Next() % 1024 - 512) + position.y);
        const auto cellX = static_cast<std::uint8_t>(static_cast<int>(static_cast<double>(x) * Reciprocal));
        const auto cellY = static_cast<std::uint8_t>(static_cast<int>(static_cast<double>(y) * Reciprocal));
        const auto attribute = selectionTerrain->attributes->cells[Client::Data::Terrain::CellIndex(cellX, cellY)];
        if (attribute != 0 && attribute != 2)
            continue;
        ++admittedFish;
        const auto timer = expected.random.Next() % 128;
        ASSERT_GT(timer, 1) << "la precondición contractual evita el RNG de expiración";
        Advance(expected.random, 2); // FullMovement's initial scale and length with speed kept at 0.
    }
    // 54: newborn leaves with no RNG; counter 8; TerrainWall 8; own body 3848+1; seen cell 8+1; Arrival 1; particle
    // wind 2. MoveHero's return skips 0x57cfc0.
    constexpr int AfterFishBeforeCloth = 8 + 8 + Client::Models::OwnRecordCloseRandoms + 1 + 8 + 1 + 1 + 2;
    Advance(expected.random, AfterFishBeforeCloth);
    Client::Models::StepTheClothWorld(expected);
    Advance(expected.random, 4); // PrepareFrame queries/closes the wait after the cloth.
    Client::App::GameLoop loop(online.clock, manager, Client::Kernel::PacingMode::Faithful);

    ASSERT_EQ(loop.RunFrame(Client::Platform::WindowState{}, input), 1);

    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    ASSERT_EQ(game->Failure(), std::nullopt);
    EXPECT_EQ(online.session.State(), Client::Session::SessionState::EnteringWorld);
    EXPECT_EQ(world.Runtime().warpCounter, Client::Kernel::WorldLoadingWait - 1);
    EXPECT_EQ(world.Runtime().warpDepth, 0);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(cloth.logicSteps, before.logicSteps + 1);
    EXPECT_EQ(cloth.windBase, expected.windBase);
    EXPECT_EQ(cloth.wind, expected.wind);
    EXPECT_EQ(cloth.random.State(), expected.random.State());
    EXPECT_EQ(hero.revision, revision);
    EXPECT_EQ(hero.position, position);
    EXPECT_TRUE(hero.seen);
    EXPECT_GT(hero.animation.frame, animation.frame);
    EXPECT_EQ(std::ranges::count_if(world.Fauna().Fish(), [](const auto& fish) { return fish.active != 0; }),
              admittedFish);
    for (const auto& fish : world.Fauna().Fish())
    {
        if (fish.active != 0)
        {
            EXPECT_EQ(fish.model, 0) << "54 conserva el tipo del slot BSS, resuelto por Object55 real";
        }
    }
    EXPECT_EQ(effect.life, 19);
    EXPECT_TRUE(effect.alive);
    EXPECT_EQ(world.Runtime().leafWeather.phase, 20); // 530564..530585: phase=(phase+20)%2000.
    EXPECT_EQ(std::ranges::count_if(world.Leaves().All(), [](const auto& leaf) { return leaf.alive; }), 80);
    EXPECT_EQ(selection.Terrain(), selectionTerrain);
    EXPECT_EQ(selectionTerrain->heights->cells[0], heightBefore);
    EXPECT_EQ(selectionTerrain->attributes->cells[0], cellBefore | Client::Pathfinding::CharacterWallBit);
    EXPECT_EQ(selection.Terrain(), &states.terrain.current);
    EXPECT_EQ(world.Runtime().map, 54);
    EXPECT_EQ(states.camera.trapezoid.x, polygon.x);
    EXPECT_EQ(states.camera.trapezoid.y, polygon.y);
    EXPECT_TRUE(manager.HoldsTheLastFrame());
    EXPECT_FALSE(game->MouseRay().has_value());
    ASSERT_EQ(target.frames.size(), 1u) << "GameLoop no llama BeginFrame/Present mientras se retiene";
    EXPECT_TRUE(target.frames.front().presented);
    EXPECT_EQ(target.frames.front().events.size(), mosaicEvents);
}
} // namespace
