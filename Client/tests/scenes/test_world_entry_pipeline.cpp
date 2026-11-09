#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Network/PacketSerials.h"
#include "Network/Crypto/Xor32.h"
#include "support/SameBytes.h"
#include "support/Bytes.h"
#include "Input/InputState.h"
#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Render/Models/Cloth.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SelectionState.h"
#include "Scenes/LeafDrawingServices.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldResources.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"
#include "Render/Pass3D.h"
#include "World/ModelEffects.h"
#include "World/PortableLeafWaveEvaluator.h"
#include "support/FakeClock.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <array>
#include <cstdint>
#include <chrono>
#include <functional>
#include <vector>

namespace
{
class WorldEntryPipelineTest : public Client::Tests::GoldenDataTest
{
};

TEST_F(WorldEntryPipelineTest, AppliesTheEncryptedEntryBeforeAZeroStepRenderUsingTheDurableOwner)
{
    Client::Tests::FakeTextureStore textures;
    Client::Tests::FakeClock clock;
    Client::Render::Models::ClothWorld cloth{
        .random = Client::Render::Models::CrtRandom(12345), .logicSteps = 370, .openCounterAccesses = 1};
    Client::Scenes::WorldState world(textures);
    Client::Scenes::SelectionState selection(world);
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Tests::LoggedInSession logging;
    Client::Input::InputState input;
    Client::Scenes::GameModels models;
    Client::Tests::RecordingFrameTarget target;
    Client::World::PortableLeafWaveEvaluator leafWaves;
    Client::Render::Models::OriginalMatrix leafView{};
    Client::Render::ModelBatchParticleBackend leafBackend{std::ref(target.Models()), std::cref(models),
                                                          Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices leafDrawing{std::cref(models), std::ref(leafBackend), std::cref(leafView)};
    world.Runtime().worldTime = 40.0f; // T explícito del fixture; qD/clock no es su productor.
    const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                               .textures = textures,
                                               .log = logging.log,
                                               .clock = &clock,
                                               .cloth = &cloth,
                                               .input = &input,
                                               .camera = &camera,
                                               .cameraInput = &cameraInput,
                                               .selection = &selection,
                                               .world = &world,
                                               .leafWaves = &leafWaves,
                                               .worldTextureModels = &models,
                                               .modelBatch = &target.Models(),
                                               .leafView = &leafView,
                                               .leafDrawing = &leafDrawing};
    const auto data = Client::Scenes::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    const auto loadedModels = models.Load(m_dataRoot, textures, logging.log);
    ASSERT_TRUE(loadedModels.HasValue()) << loadedModels.Error();
    Client::Scenes::SessionEventApplier applier(selection, camera, cameraInput, context, data.Value(), models);
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    world.Runtime().drawingEnabled = true;
    world.Runtime().ownRecordDepth = 1;
    world.Runtime().warpDepth = 255;
    auto& own = world.OwnCharacter();
    own.name[0] = std::byte{'T'};
    own.level = 77;
    own.characterClass = 4;
    own.equipment[2].id = 0x0e00;
    own.equipment[2].scalar1Low = 0x78;
    own.equipment[2].flags1b = 0x80;
    Client::Render::Models::CrtRandom expected(12345);
    for (int i = 0; i < 65536; ++i)
        static_cast<void>(expected.Next());
    const auto heroIndex = static_cast<std::size_t>(expected.Next() % 400);
    Client::Network::PacketSerials serials;
    std::vector<std::byte> reply(52);
    reply[0] = std::byte{0xc1};
    reply[1] = std::byte{52};
    reply[2] = std::byte{0xf3};
    reply[3] = std::byte{3};
    reply[4] = std::byte{133};
    reply[5] = std::byte{127};
    reply[6] = std::byte{0};
    reply[7] = std::byte{0};
    reply[8] = std::byte{0x78};
    reply[9] = std::byte{0x56};
    reply[10] = std::byte{0x34};
    reply[11] = std::byte{0x12};
    reply[44] = std::byte{0x12};
    reply[45] = std::byte{0x34};
    online.socket.reads = {Client::Tests::FromTheGameServer(reply, serials)};
    const auto applied = online.session.Poll();
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(online.session.State(), Client::App::SessionState::InWorld);
    EXPECT_EQ(world.Runtime().hero, heroIndex);
    EXPECT_EQ(world.Runtime().map, 0);
    EXPECT_EQ(world.Runtime().warpCounter, 30);
    const auto& hero = world.Actors()[heroIndex];
    EXPECT_TRUE(hero.active);
    EXPECT_EQ(hero.position.x, 13350.0f);
    EXPECT_EQ(hero.position.y, 12750.0f);
    EXPECT_EQ(hero.angles.z, -45.0f);
    EXPECT_EQ(hero.logicStamp, 370);
    EXPECT_EQ(hero.alpha, 0.0f);
    EXPECT_EQ(hero.appearance.bodyParts[0].model, 0x1012);
    EXPECT_EQ(hero.appearance.bodyParts[0].level, 15);
    EXPECT_TRUE(hero.appearance.bodyParts[0].excellent);
    EXPECT_EQ(own.experience, 0x12345678u);
    EXPECT_EQ(own.equipment[2].id, 0xffff);
    ASSERT_NE(world.Resources()->terrain.Terrain(), nullptr);
    EXPECT_EQ(world.Resources()->terrain.LoadedMap(), 0);
    EXPECT_EQ(selection.Terrain(), world.Resources()->terrain.Terrain());
    EXPECT_GT(world.Resources()->models.Count(), 0u);
    EXPECT_EQ(cloth.logicSteps, 370);
    int arrivals = 0;
    for (const auto& effect : world.Resources()->effects.Effects())
        if (effect.alive && effect.type == 0x61f)
        {
            ++arrivals;
            EXPECT_EQ(effect.life, 20);
            EXPECT_FALSE(effect.visible);
        }
    EXPECT_EQ(arrivals, 2);
    Client::Scenes::GameScene scene(context, &online.session, &models, &data.Value());
    ASSERT_TRUE(scene.Enter().HasValue());
    scene.PrepareFrame();
    ASSERT_FALSE(scene.HoldsTheLastFrame());
    auto& canvas = target.BeginFrame(clock.Now());
    scene.Render(canvas, 0.9f);
    target.Present();
    std::size_t vertices = 0;
    for (const auto& event : target.frames.back().events)
        vertices += event.modelVertices.size();
    EXPECT_GT(vertices, 0u);
    EXPECT_EQ(cameraInput.hero, hero.position);
    ASSERT_TRUE(world.Resources()->pickingRay.has_value());
    for (const auto& effect : world.Resources()->effects.Effects())
        if (effect.alive && effect.type == 0x61f)
        {
            EXPECT_TRUE(effect.visible);
        }
    clock.Advance(std::chrono::milliseconds{5555});
    online.socket.sent.clear();
    const std::vector<std::byte> teleport{std::byte{0xc1}, std::byte{8},   std::byte{0x1c}, std::byte{7},
                                          std::byte{0},    std::byte{131}, std::byte{126},  std::byte{15}};
    online.socket.reads = {Client::Tests::FromTheGameServer(teleport, serials)};
    const auto warped = online.session.Poll();
    ASSERT_TRUE(warped.HasValue()) << warped.Error();
    EXPECT_EQ(hero.position.x, 13150.0f);
    EXPECT_EQ(hero.position.y, 12650.0f);
    EXPECT_EQ(hero.angles.z, 630.0f);
    EXPECT_EQ(hero.alpha, 0.0f);
    EXPECT_EQ(world.Runtime().warpCounter, 30);
    EXPECT_EQ(world.Runtime().lastTeleportTick, 5555u);
    EXPECT_FALSE(online.socket.sent.empty());

    // F304 claro reconstruye S y conservaW, sin confirmaciónF3/12.
    world.Runtime().warpCounter = 42;
    online.socket.sent.clear();
    std::vector<std::byte> regen(24);
    regen[0] = std::byte{0xc1};
    regen[1] = std::byte{24};
    regen[2] = std::byte{0xf3};
    regen[3] = std::byte{4};
    regen[4] = std::byte{129};
    regen[5] = std::byte{125};
    regen[7] = std::byte{0};
    regen[8] = std::byte{0x56};
    regen[9] = std::byte{0x34};
    regen[10] = std::byte{0x9a};
    regen[11] = std::byte{0x78};
    regen[12] = std::byte{0xde};
    regen[13] = std::byte{0xbc};
    regen[16] = std::byte{0x78};
    regen[17] = std::byte{0x56};
    regen[18] = std::byte{0x34};
    regen[19] = std::byte{0x12};
    regen[20] = std::byte{0xf0};
    regen[21] = std::byte{0xde};
    regen[22] = std::byte{0xbc};
    regen[23] = std::byte{0x9a};
    online.socket.reads = {regen};
    const auto revived = online.session.Poll();
    ASSERT_TRUE(revived.HasValue()) << revived.Error();
    EXPECT_EQ(world.Runtime().hero, heroIndex);
    EXPECT_EQ(world.Runtime().actorConstructionIndex, heroIndex);
    EXPECT_EQ(hero.position.x, 12950.0f);
    EXPECT_EQ(hero.position.y, 12550.0f);
    EXPECT_EQ(hero.angles.z, -45.0f);
    EXPECT_TRUE(hero.active);
    EXPECT_EQ(hero.safeZone, 1);
    EXPECT_EQ(hero.alpha, 1.0f);
    EXPECT_EQ(world.Runtime().warpCounter, 42);
    EXPECT_TRUE(online.socket.sent.empty());
    EXPECT_EQ(own.life, 0x3456);
    EXPECT_EQ(own.mana, 0x789a);
    EXPECT_EQ(own.bp, 0xbcde);
    EXPECT_EQ(own.experience, 0x12345678u);
    EXPECT_EQ(own.zen, 0x9abcdef0u);

    auto& walking = world.Actors()[heroIndex];
    walking.path = {.current = 0, .progress = 0, .count = 2, .cells = {{{129, 125}, {130, 125}}}};
    walking.moving = true;
    world.Runtime().warpCounter = 0;
    scene.Step(Client::Input::InputState{});
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_TRUE(walking.moving);
    EXPECT_EQ(walking.cellX, 130u);
    EXPECT_EQ(walking.cellY, 125u);
    EXPECT_FLOAT_EQ(walking.position.x, 12962.0f);
    EXPECT_FLOAT_EQ(walking.position.y, 12550.0f);
    EXPECT_EQ(walking.animation.body.action, 0xd);
    EXPECT_FLOAT_EQ(walking.movementPhase, 0.15f);

    // Press+release antes de la cadencia queda pendiente aunque GameLoop consuma los flancos.
    walking.moving = false;
    walking.cellX = 130;
    walking.cellY = 125;
    walking.position.x = 13050.0f;
    walking.position.y = 12550.0f;
    walking.alpha = 1.0f;
    world.Runtime().mouseUpdateTime = 5;
    world.Runtime().mouseUpdateTimeMax = 6;
    auto& resources = *world.Resources();
    scene.PrepareFrame();
    auto& cameraCanvas = target.BeginFrame(clock.Now());
    scene.Render(cameraCanvas, 0.0f);
    target.Present();
    const auto pass =
        Client::Render::MakePass3D({640, 480}, {0, 0, 640, 432}, Client::World::GameCameraOf(camera, cameraInput));
    const auto* terrain = resources.terrain.Terrain();
    const float targetHeight = static_cast<float>(Client::World::TerrainHeightAt(
        *terrain->heights, *terrain->attributes, 13125.0f, 12570.0f, camera.fixedHeight));
    const auto pixel = pass.Project({13125.0f, 12570.0f, targetHeight});
    ASSERT_TRUE(pixel.has_value());
    input.Apply({.kind = Client::Input::InputEvent::Kind::Motion, .x = pixel->x, .y = pixel->y});
    auto& clickCanvas = target.BeginFrame(clock.Now());
    scene.Render(clickCanvas, 0.0f); // Publica el rayo y los caches reales del frame anterior al clic.
    target.Present();
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    ASSERT_TRUE(resources.pickingRay.has_value());
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonDown, .button = Client::Input::MouseButton::Left});
    input.Apply({.kind = Client::Input::InputEvent::Kind::ButtonUp, .button = Client::Input::MouseButton::Left});
    online.socket.sent.clear();
    scene.Step(input);
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_FALSE(walking.moving);
    EXPECT_TRUE(world.Runtime().pendingLeftPress);
    EXPECT_EQ(world.Runtime().mouseUpdateTime, 6u);
    input.ConsumeEdges();
    scene.Step(input);
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_TRUE(walking.moving);
    EXPECT_FALSE(world.Runtime().pendingLeftPress);
    EXPECT_EQ(walking.movementIntent, 0);
    EXPECT_EQ(walking.path.count, 2);
    EXPECT_EQ(walking.path.cells[0].x, 130);
    EXPECT_EQ(walking.path.cells[1].x, 131);
    EXPECT_FLOAT_EQ(walking.position.x, 13050.0f); // La ruta nueva avanza recién en el paso siguiente.
    EXPECT_EQ(world.Runtime().mouseUpdateTimeMax, 0u);
    EXPECT_EQ(world.Runtime().mouseUpdateTime, 1u);
    EXPECT_FALSE(online.socket.sent.empty());

    // 6374c6 consulta el mapa vivo:10 no dibuja el terreno aunque el cache anterior seaLorencia.
    world.Runtime().map = 10;
    walking.alpha = 0.0f;
    scene.PrepareFrame();
    auto& noTerrain = target.BeginFrame(clock.Now());
    scene.Render(noTerrain, 0.0f);
    target.Present();
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    std::size_t renderedModels = 0;
    for (const auto& event : target.frames.back().events)
        for (const auto& command : event.modelCommands)
        {
            ++renderedModels;
            if (!command.state.texture)
                continue;
            const auto slot = command.state.texture->value;
            EXPECT_FALSE((slot >= 0x23 && slot <= 0x30) || slot == 0x122);
        }
    EXPECT_GT(renderedModels, 0u); // Otros pases continúan; no se admite resolverlo reteniendo elframe.

    // El pase real usa el nivel persistente y la comparación de cámara, no el wrapper nivel0.
    resources.terrainPolygonState.effectiveLevel = 2;
    camera.distance = 2000.0f;
    camera.targetDistance = 1000.0f;
    camera.level = 1;
    auto& retainedPolygon = target.BeginFrame(clock.Now());
    scene.Render(retainedPolygon, 0.0f);
    target.Present();
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_EQ(resources.terrainPolygonState.effectiveLevel, 2);
    // Nivel2: (-1430,1590) girado45 queda a más de20celdas a la izquierda del héroe.
    // Nivel0: (-1190,1272) queda a menos de20; el umbral discrimina sin consultar el port.
    const float leftBoundary = walking.position.x * 0.01f - 20.0f;
    EXPECT_LT(resources.terrainPolygon.x[0], leftBoundary);
    camera.distance = 1000.9f;
    camera.targetDistance = 1000.1f;
    auto& adoptedPolygon = target.BeginFrame(clock.Now());
    scene.Render(adoptedPolygon, 0.0f);
    target.Present();
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_EQ(resources.terrainPolygonState.effectiveLevel, 1);
    EXPECT_GT(resources.terrainPolygon.x[0], leftBoundary);
}
TEST_F(WorldEntryPipelineTest, ConstructsRealMapsAndRequestsCrywolfStateOnlyForThirtyFour)
{
    Client::Tests::FakeTextureStore textures;
    Client::Tests::LoggedInSession logging;
    const auto data = Client::Scenes::LoadGameData(m_dataRoot);
    ASSERT_TRUE(data.HasValue()) << data.Error();
    Client::Scenes::GameModels models;
    const auto loaded = models.Load(m_dataRoot, textures, logging.log);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint8_t map : std::array<std::uint8_t, 5>{0, 1, 2, 3, 34})
    {
        SCOPED_TRACE(map);
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(69)};
        Client::Scenes::WorldState world(textures);
        Client::Scenes::SelectionState selection(world);
        Client::World::GameCamera camera;
        Client::World::GameCameraInput cameraInput;
        const Client::Scenes::SceneContext context{.dataRoot = m_dataRoot,
                                                   .textures = textures,
                                                   .log = logging.log,
                                                   .cloth = &cloth,
                                                   .camera = &camera,
                                                   .cameraInput = &cameraInput,
                                                   .selection = &selection,
                                                   .world = &world,
                                                   .worldTextureModels = &models};
        Client::Scenes::SessionEventApplier applier(selection, camera, cameraInput, context, data.Value(), models);
        Client::Tests::LoggedInSession online(&applier);
        ASSERT_NO_FATAL_FAILURE(online.LogIn());
        online.socket.sent.clear();
        world.Runtime().ownRecordDepth = 1;
        world.Runtime().warpDepth = 1;
        if (map == 0)
        {
            Client::Render::Models::CrtRandom predicted(69);
            for (int i = 0; i < 65536; ++i)
                static_cast<void>(predicted.Next());
            auto& previous = world.Actors()[static_cast<std::size_t>(predicted.Next() % 400)];
            previous.pet.model = 0x1c15;
            previous.previousHad1c15 = false; // La fuente de511290 es el WORD318, no esta caché legacy.
        }
        std::vector<std::byte> packet(52);
        packet[0] = std::byte{0xc1};
        packet[1] = std::byte{52};
        packet[2] = std::byte{0xf3};
        packet[3] = std::byte{3};
        packet[4] = std::byte{100};
        packet[5] = std::byte{100};
        packet[6] = static_cast<std::byte>(map);
        packet[7] = std::byte{1};
        Client::Network::PacketSerials incoming;
        online.socket.reads = {Client::Tests::FromTheGameServer(packet, incoming)};
        const auto applied = online.session.Poll();
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(online.session.State(), Client::App::SessionState::InWorld);
        EXPECT_EQ(world.Resources()->terrain.LoadedMap(), map);
        EXPECT_EQ(world.Runtime().map, map);
        const auto& hero = world.Actors()[world.Runtime().hero];
        EXPECT_TRUE(hero.active);
        EXPECT_EQ(hero.cellX, 100u);
        EXPECT_EQ(hero.cellY, 100u);
        EXPECT_EQ(hero.targetX, 100u);
        EXPECT_EQ(hero.targetY, 100u);
        EXPECT_EQ(hero.angles.z, 0.0f);
        if (map == 0)
        {
            ASSERT_EQ(hero.safeZone, 0);
            const auto& terrain = *world.Resources()->terrain.Terrain();
            const double base = Client::World::TerrainHeightAt(*terrain.heights, *terrain.attributes, 10050.0f,
                                                               10050.0f, camera.fixedHeight);
            EXPECT_EQ(hero.position.z, static_cast<float>(base + 30.0));
        }
        Client::Network::PacketSerials outgoing;
        const auto sent = Client::Tests::AsTheGameServerReceives(online.socket.sent, outgoing);
        ASSERT_EQ(sent.packets.size(), map == 34 ? 1u : 0u);
        if (map == 34)
        {
            EXPECT_FALSE(sent.packets[0].WasDecrypted());
            std::vector<std::byte> logical(sent.packets[0].Bytes().begin(), sent.packets[0].Bytes().end());
            Client::Network::Crypto::Xor32DecodeBytes(logical, 3, 0);
            EXPECT_TRUE(Client::Tests::SameBytes(logical, Client::Tests::Bytes({0xc1, 4, 0xbd, 0})));
        }
    }
}
} // namespace
