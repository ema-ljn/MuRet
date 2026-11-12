#include "Scenes/GameScene.h"

#include "App/CharacterEvent.h"
#include "App/GameLoop.h"
#include "App/PacingMode.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/InputState.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameData.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameModels.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/TerrainWalls.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"

#include "support/CharacterPackets.h"
#include "support/GoldenDir.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingFrameTarget.h"
#include "support/SessionHarness.h"

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

void Advance(Client::Render::Models::CrtRandom& random, int count)
{
    for (int index = 0; index < count; ++index)
        static_cast<void>(random.Next());
}

// 633f20 deja World54 en SelectionState; 6356f0 presenta el mosaico y 636290 empieza
// a esperar F303 sin cambiarlo. El retorno temprano de MoveHero no termina el paso de escena.
TEST_F(InitialGameWaitTest, TheFirstGameStepKeepsSelectionTerrainAndStepsActorsEffectsAndClothBeforeF303)
{
    Client::Tests::LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    Client::Tests::CharacterEntryFields entry;
    entry.equipment[4] = 3; // Pet NONE, sin helper: lista contractual en el wire real del GS falso.
    online.ReceiveTheList({entry});
    ASSERT_TRUE(online.session.Characters().has_value());
    online.session.ChooseCharacter(std::uint8_t{0});

    Client::Tests::FakeTextureStore textures;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(0x1234)};
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    auto data = Client::Scenes::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    Client::Scenes::GameModels models;
    const auto loadedModels = models.Load(m_dataRoot, textures, online.log);
    ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
    const auto* player = models.SkeletonOf(Client::Data::Models::PlayerSkeletonModel);
    ASSERT_NE(player, nullptr);

    // El constructor de la lista lee el terreno del login anterior; InitializeSelection carga
    // después el54 real. No se presta un esqueleto sintético ni un TerrainWall vacío de juego.
    auto loginTerrain = Client::World::LoadWorldTerrain(m_dataRoot, 55);
    ASSERT_TRUE(loginTerrain.HasValue()) << loginTerrain.Error();
    selection.RetainTerrain(55, std::move(loginTerrain).Value());
    selection.BeginSelection();
    const std::array<Client::App::CharacterEvent, 1> events{
        Client::App::CharacterListReceived{*online.session.Characters()}};
    const auto applied = selection.Apply(events, camera, cameraInput, &cloth, player);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const auto initialized = selection.InitializeSelection(m_dataRoot, cameraInput);
    ASSERT_TRUE(initialized.HasValue()) << initialized.Error();
    selection.DrawSelection(cameraInput);
    world.Runtime().terrainPolygon = Client::World::TerrainPolygonOf(Client::World::CullingScene::CharacterSelect, 54,
                                                                     Client::Scenes::SelectionCullingPoint,
                                                                     Client::Scenes::SelectionCamera().angles.z);
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
    world.PrepareSelectedActor(*online.session.ChosenCharacter(), cloth.random);
    const auto polygon = world.Runtime().terrainPolygon;
    const auto* selectionTerrain = selection.Terrain();
    ASSERT_NE(selectionTerrain, nullptr);
    ASSERT_NE(selectionTerrain->heights, nullptr);
    ASSERT_NE(selectionTerrain->attributes, nullptr);
    // 0x633f4e carga el 54 en los globales del mundo (0x61a560): la selección presta ese terreno.
    ASSERT_EQ(selectionTerrain, &world.Map().terrain);
    ASSERT_EQ(world.Runtime().map, 54);
    ASSERT_TRUE(world.Runtime().drawingEnabled);
    auto& hero = world.Actors()[world.Runtime().hero];
    ASSERT_TRUE(hero.alive);
    ASSERT_EQ(hero.type, Client::Data::Models::PlayerSkeletonModel);
    const auto revision = hero.revision;
    const auto position = hero.position;
    const auto animation = hero.animation;
    const auto cellBefore = selectionTerrain->attributes->cells[0];
    const auto heightBefore = selectionTerrain->heights->cells[0];

    // Probe contractual de la cola viva: Arrival usa su creador nativo sin BMD ni animación
    // inventados. No se afirma que F303 haya llegado ni que cree este efecto durante la espera.
    const auto effectSlot = world.Map().effects.Create(Client::World::ArrivalEffect, position, {}, {1, 1, 1}, 1);
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
                                               .world = &world};
    Client::Scenes::GameScene* game = nullptr;
    Client::Tests::RecordingFrameTarget target;
    Client::Scenes::SceneManager manager(
        [&](Client::Scenes::SceneId scene) -> std::unique_ptr<Client::Scenes::IScene>
        {
            if (scene == Client::Scenes::SceneId::WorldLoading)
                return std::make_unique<Client::Scenes::WorldLoadingScene>(context);
            auto created = std::make_unique<Client::Scenes::GameScene>(
                context, &online.session, &models, &data.Value().items->table, &models.TextureSlots(),
                &*data.Value().gates, &*data.Value().skills);
            game = created.get();
            return created;
        },
        target, online.clock, nullptr, online.log);
    const auto started = manager.Start(Client::Scenes::SceneId::WorldLoading);
    ASSERT_TRUE(started.HasValue()) << started.Error();
    manager.Render(0);
    ASSERT_EQ(manager.Failure(), std::nullopt);
    ASSERT_EQ(target.frames.size(), 1u);
    ASSERT_TRUE(target.frames.front().presented);
    const auto mosaicEvents = target.frames.front().events.size();
    manager.Step(input); // El paso de escena3 pasa a5 después de presentar el mosaico.
    ASSERT_EQ(manager.Current(), Client::Scenes::SceneId::Game);
    ASSERT_NE(game, nullptr);
    ASSERT_EQ(world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait);
    const auto before = cloth;
    Client::Render::Models::ClothWorld expected = before;
    // 447108..4471b2:54 admite el fallback de425070 sólo con atributo0/2. La CRT
    // anterior determina candidatos y su admisión contra el .att real, antes de observar GameScene.
    // El slot BSS conserva type/speed0: si se admite toma timer/scale y un rand de FullMovement;
    // este fixture exige timer>1, sin sorteo de expiración. No crea un tipo ni un BMD artificial.
    constexpr float Reciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});
    // 0x636ded..0x63705b: la UI del paso con todo cerrado (la barra de skills, Q/W/E e IsOpen(0)), antes de los
    // objetos y de la fauna.
    Advance(expected.random, Client::Render::Models::OwnRecordCloseRandoms + 2 + 2);
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
        Advance(expected.random, 2); // escala inicial y longitud de FullMovement con speed retenido0.
    }
    // 54: hojas recién nacidas sin RNG; contador8; TerrainWall8; cuerpo propio3848+1;
    // celda vista8+1; Arrival1; viento de partículas2. El retorno de MoveHero omite57cfc0.
    constexpr int AfterFishBeforeCloth = 8 + 8 + Client::Render::Models::OwnRecordCloseRandoms + 1 + 8 + 1 + 1 + 2;
    Advance(expected.random, AfterFishBeforeCloth);
    Client::Render::Models::StepTheClothWorld(expected);
    Advance(expected.random, 4); // PrepareFrame consulta/cierra espera después de la tela.
    Client::App::GameLoop loop(online.clock, manager, Client::App::PacingMode::Faithful);

    ASSERT_EQ(loop.RunFrame(Client::App::WindowState{}, input), 1);

    ASSERT_FALSE(manager.Failure().has_value()) << *manager.Failure();
    ASSERT_EQ(game->Failure(), std::nullopt);
    EXPECT_EQ(online.session.State(), Client::App::SessionState::EnteringWorld);
    EXPECT_EQ(world.Runtime().warpCounter, Client::Scenes::WorldLoadingWait - 1);
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
            EXPECT_EQ(fish.type, 0) << "54 conserva el tipo del slot BSS, resuelto por Object55 real";
        }
    }
    EXPECT_EQ(effect.life, 19);
    EXPECT_TRUE(effect.alive);
    EXPECT_EQ(world.Runtime().leafWeather.phase, 20); // 530564..530585: phase=(phase+20)%2000.
    EXPECT_EQ(std::ranges::count_if(world.Leaves().All(), [](const auto& leaf) { return leaf.alive; }), 80);
    EXPECT_EQ(selection.Terrain(), selectionTerrain);
    EXPECT_EQ(selectionTerrain->heights->cells[0], heightBefore);
    EXPECT_EQ(selectionTerrain->attributes->cells[0], cellBefore | Client::World::CharacterWallBit);
    EXPECT_EQ(selection.Terrain(), &world.Map().terrain);
    EXPECT_EQ(world.Runtime().map, 54);
    EXPECT_EQ(world.Runtime().terrainPolygon.x, polygon.x);
    EXPECT_EQ(world.Runtime().terrainPolygon.y, polygon.y);
    EXPECT_TRUE(manager.HoldsTheLastFrame());
    EXPECT_FALSE(game->MouseRay().has_value());
    ASSERT_EQ(target.frames.size(), 1u) << "GameLoop no llama BeginFrame/Present mientras se retiene";
    EXPECT_TRUE(target.frames.front().presented);
    EXPECT_EQ(target.frames.front().events.size(), mosaicEvents);
}
} // namespace
