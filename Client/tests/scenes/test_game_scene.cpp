#include "Scenes/GameScene.h"

#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameData.h"
#include "Render/Models/Skeleton.h"
#include "Data/Models/Model.h"
#include "Render/TextureSlot.h"

#include "App/SessionState.h"
#include "Input/InputState.h"
#include "Input/InputEvent.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldResources.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/PortableLeafWaveEvaluator.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/LeafWeather.h"
#include "World/Leaves.h"
#include <glm/ext/vector_float3.hpp>
#include "Scenes/WorldRuntime.h"
#include "Render/Models/Cloth.h"
#include <array>

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/SameBytes.h"
#include "support/TestGameServer.h"
#include "support/FakeTextureStore.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Client::Scenes::GameScene: la escena 5 mínima, que manda el F3 03 en su primer paso (0x636290).
namespace
{
using Client::App::SessionState;
using Client::Scenes::GameScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Tests::FakeTextureStore;
using Client::Tests::LoggedInSession;

// Con el login, la lista y el lugar 3 elegido: la sesión espera la entrada.
void ChooseSlotThree(LoggedInSession& online, Client::Scenes::WorldState& world)
{
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}, {3, "char0002"}});
    online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
    world.ReconstructSelectionActor(*online.session.ChosenCharacter(), {});
    world.SelectActor(3);
}

TEST(GameSceneTest, TheFirstStepSendsF303)
{
    LoggedInSession online;
    Client::Scenes::WorldState world;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online, world));
    FakeTextureStore store;
    GameScene scene(SceneContext{.dataRoot = {}, .textures = store, .log = online.log, .world = &world},
                    &online.session);
    ASSERT_TRUE(scene.Enter().HasValue());
    const Client::Input::InputState input;

    scene.Step(input);
    scene.Step(input);

    EXPECT_EQ(online.session.State(), SessionState::EnteringWorld);
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("se ignora la entrada al mundo") != std::string::npos; }));
}

// 636495..63661e, extremos del dominio nativo L=0..10: sin undécimo byte en el request.
TEST(GameSceneTest, TheActorNameRequestPadsEmptyAndUsesAllTenFullBytes)
{
    for (const std::string_view name : {std::string_view("\0QWERTYUIO", 10), std::string_view("ABCDEFGHIJ", 10)})
    {
        LoggedInSession online;
        Client::Scenes::WorldState world;
        ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online, world));
        const auto raw = Client::Tests::NameBytes(name);
        std::ranges::copy(raw, world.Actors()[3].name.begin());
        FakeTextureStore store;
        GameScene scene(SceneContext{.dataRoot = {}, .textures = store, .log = online.log, .world = &world},
                        &online.session);
        ASSERT_TRUE(scene.Enter().HasValue());
        online.socket.sent.clear();
        scene.Step(Client::Input::InputState{});
        Client::Network::PacketSerials serials;
        const auto sent = Client::Tests::AsTheGameServerReceives(online.socket.sent, serials);
        ASSERT_EQ(sent.packets.size(), 1u);
        const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(sent.packets.front());
        const auto expected =
            name.front() == '\0'
                ? Client::Tests::Bytes({0xc1, 0x0e, 0xf3, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})
                : Client::Tests::Bytes({0xc1, 0x0e, 0xf3, 0x03, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'});
        EXPECT_TRUE(Client::Tests::SameBytes(clear.Bytes(), expected));
    }
}

// Q-C70: en el juego (el modo 5), el FD_CLOSE no abre un aviso y el juego sigue sin conexión: el
// texto 3 en el área de mensajes es de la CL9. La escena no vuelve al login.
TEST(GameSceneTest, AFailedSessionKeepsTheGame)
{
    LoggedInSession online;
    Client::Scenes::WorldState world;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online, world));
    FakeTextureStore store;
    GameScene scene(SceneContext{.dataRoot = {}, .textures = store, .log = online.log, .world = &world},
                    &online.session);
    const Client::Input::InputState input;
    scene.Step(input);

    online.socket.peerClosed = true;
    static_cast<void>(online.session.Poll());
    scene.Step(input);

    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(scene.Next(), std::nullopt);
    EXPECT_FALSE(scene.WantsToQuit());
}

// El original loguea el lugar y el nombre; MuRet, el lugar (el nombre es del jugador).
TEST(GameSceneTest, TheLogHasTheSlotNotTheName)
{
    LoggedInSession online;
    Client::Scenes::WorldState world;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online, world));
    FakeTextureStore store;
    GameScene scene(SceneContext{.dataRoot = {}, .textures = store, .log = online.log, .world = &world},
                    &online.session);
    const Client::Input::InputState input;

    scene.Step(input);

    EXPECT_TRUE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                    { return line.find("del lugar 3") != std::string::npos; }));
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}
class MinimalPlayerLibrary final : public Client::Scenes::IModelLibrary
{
public:
    MinimalPlayerLibrary()
    {
        skeleton.actions.resize(256);
        skeleton.actions[0xc2] = {.repeats = false, .speed = 1.0f, .keyCount = 4};
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x20a ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton;
};

TEST(GameSceneTest, ParticleStepBorrowsTheRawWorldMapAndRunsTheExpirationTailOnce)
{
    for (const std::int32_t map : {34, 0x10000022})
    {
        LoggedInSession online;
        FakeTextureStore textures;
        Client::Scenes::WorldState world(textures);
        world.Runtime().drawingEnabled = true;
        world.Runtime().map = map;
        world.Runtime().worldTime = 40; // qT ya publicado, separado del reloj relativo del fixture.
        world.Actors()[0].dying = true; // MoveHero retorna y todos los actores están inactivos.
        auto& particle = world.Resources()->particles.All()[0];
        particle = Client::World::Particle{.alive = true,
                                           .type = 0x68,
                                           .subtype = 0,
                                           .scale = 1,
                                           .position = {1, 2, 3},
                                           .light = {2, 3, 4},
                                           .life = 1,
                                           .moves = true,
                                           .velocity = {4, 5, 6}};
        Client::Render::Models::ClothWorld cloth;
        MinimalPlayerLibrary models;
        Client::World::PortableParticleTrigonometry particleMath;
        Client::World::PortableLeafWaveEvaluator leafWaves;
        GameScene scene(SceneContext{.dataRoot = {},
                                     .textures = textures,
                                     .log = online.log,
                                     .cloth = &cloth,
                                     .world = &world,
                                     .leafWaves = &leafWaves,
                                     .particleTrigonometry = &particleMath},
                        nullptr, &models);
        scene.Step(Client::Input::InputState{});
        ASSERT_EQ(scene.Failure(), std::nullopt);
        // 530490 ejecutado en34; rawextendido conserva el bypass del gate, sin truncar a byte.
        EXPECT_EQ(world.Runtime().leafWeather.phase, map == 34 ? 20 : 0);
        EXPECT_EQ(world.Resources()->leaves.All()[0].alive, map == 34 ? 1 : 0);
        EXPECT_EQ(world.Resources()->leaves.All()[79].alive, map == 34 ? 1 : 0);
        EXPECT_EQ(world.Resources()->leaves.All()[80].alive, 0);
        EXPECT_FALSE(particle.alive);
        EXPECT_EQ(particle.life, 0);
        EXPECT_EQ(particle.position, (glm::vec3{5, 7, 9}));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
        if (map == 34)
            EXPECT_EQ(particle.light, (glm::vec3{1, 1, std::bit_cast<float>(0x3f333333u)}));
        else
        {
            EXPECT_FLOAT_EQ(particle.light.x, 1.95f);
            EXPECT_FLOAT_EQ(particle.light.y, 2.95f);
            EXPECT_FLOAT_EQ(particle.light.z, 3.95f);
        }
    }
}

TEST(GameSceneTest, ParticleStepReportsAnUnsupportedSlotAfterItsPrefixAndBeforeTheNextSlot)
{
    LoggedInSession online;
    FakeTextureStore textures;
    Client::Scenes::WorldState world(textures);
    world.Runtime().drawingEnabled = true;
    world.Runtime().worldTime = 40;
    world.Actors()[0].dying = true;
    auto slots = world.Resources()->particles.All();
    slots[0] = Client::World::Particle{
        .alive = true, .type = 0x7fffffff, .position = {1, 2, 3}, .life = 1, .moves = true, .velocity = {4, 5, 6}};
    slots[1] = Client::World::Particle{.alive = true, .type = 0x607, .life = 5};
    Client::Render::Models::ClothWorld cloth;
    MinimalPlayerLibrary models;
    Client::World::PortableParticleTrigonometry particleMath;
    GameScene scene(SceneContext{.dataRoot = {},
                                 .textures = textures,
                                 .log = online.log,
                                 .cloth = &cloth,
                                 .world = &world,
                                 .particleTrigonometry = &particleMath},
                    nullptr, &models);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_FALSE(slots[0].alive);
    EXPECT_EQ(slots[0].life, 0);
    EXPECT_EQ(slots[0].position, (glm::vec3{5, 7, 9}));
    EXPECT_TRUE(slots[1].alive);
    EXPECT_EQ(slots[1].life, 5);
    EXPECT_EQ(slots[1].position, glm::vec3(0));
}

TEST(GameSceneTest, ParticleStepStaysAfterTheModelEffectsFailureBoundary)
{
    LoggedInSession online;
    FakeTextureStore textures;
    Client::Scenes::WorldState world(textures);
    world.Runtime().drawingEnabled = true;
    world.Actors()[0].dying = true;
    auto& particle = world.Resources()->particles.All()[0];
    particle = Client::World::Particle{.alive = true, .type = 0x607, .life = 5};
    auto& effect = world.Resources()->effects.Effects()[0];
    effect.alive = true;
    effect.type = 0x135; // El efecto construido exige metadata; esta biblioteca no tiene el modelo.
    effect.life = 10;
    Client::Render::Models::ClothWorld cloth;
    MinimalPlayerLibrary models;
    GameScene scene(
        SceneContext{.dataRoot = {}, .textures = textures, .log = online.log, .cloth = &cloth, .world = &world},
        nullptr, &models);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.life, 5);
    EXPECT_EQ(particle.position, glm::vec3(0));
}

TEST(GameSceneTest, FinishingTheBodyStopsTheCanonicalActorBeforeItsHeadStep)
{
    LoggedInSession online;
    Client::Scenes::WorldState world;
    world.Runtime().drawingEnabled = true;
    world.Runtime().map = 0;
    auto& hero = world.Actors()[0];
    hero.active = true;
    hero.type = 0x20a;
    hero.run = 39;
    hero.alpha = 1.0f;
    hero.hands[0].model = hero.hands[1].model = hero.pet.model = hero.wings.model = 0xffff;
    hero.animation.body = {.action = 0xc2, .frame = 3.5f};
    hero.head.target = {90.0f, 18.0f};
    world.Runtime().warpCounter = 32;
    Client::Render::Models::ClothWorld cloth;
    FakeTextureStore textures;
    MinimalPlayerLibrary models;
    Client::Scenes::GameData data;
    GameScene scene(
        SceneContext{.dataRoot = {}, .textures = textures, .log = online.log, .cloth = &cloth, .world = &world},
        nullptr, &models, &data);
    scene.Step(Client::Input::InputState{});
    ASSERT_EQ(scene.Failure(), std::nullopt);
    EXPECT_EQ(hero.animation.body.action, 1);
    EXPECT_EQ(hero.run, 0);
    EXPECT_EQ(hero.head.angles.x, 18.0f);
    EXPECT_FLOAT_EQ(hero.head.angles.y, 3.6f);
}

TEST(GameSceneTest, HeroHeadTargetUsesMouseAndYawBeforeTheMovementPhase)
{
    LoggedInSession online;
    FakeTextureStore textures;
    Client::Scenes::WorldState world;
    world.Runtime().drawingEnabled = true;
    world.Runtime().map = 0;
    auto& hero = world.Actors()[0];
    hero.active = true;
    hero.type = 0x20a;
    hero.angles.z = 45.0f;
    hero.head.target = {27.0f, 29.0f};
    Client::Render::Models::ClothWorld cloth;
    GameScene scene(
        SceneContext{.dataRoot = {}, .textures = textures, .log = online.log, .cloth = &cloth, .world = &world},
        nullptr);
    Client::Input::InputState input;
    input.Apply({.kind = Client::Input::InputEvent::Kind::Motion, .x = 320.0f, .y = 360.0f});
    scene.Step(input);
    ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
    EXPECT_EQ(hero.head.target.x, 0.0f); // (180 + yaw45 +315)%360=180; (180+180)%360=0.
    EXPECT_EQ(hero.head.target.y, -9.0f);
    hero.animation.body.action = 0x3e;
    scene.Step(input);
    EXPECT_EQ(hero.head.target.x, 0.0f);
    EXPECT_EQ(hero.head.target.y, 0.0f);
    hero.animation.body.action = 1;
    world.Runtime().headTargetLock = -1;
    world.Runtime().targets[4] = 7;
    for (const std::int32_t map : std::array<std::int32_t, 6>{0, 6, 17, 18, 23, 24})
    {
        world.Runtime().map = map;
        hero.headTargetZ = 99.0f;
        scene.Step(input);
        ASSERT_FALSE(scene.Failure().has_value()) << scene.Failure().value_or("");
        EXPECT_EQ(hero.head.target.y, map == 6 || (map >= 18 && map <= 23) ? -9.0f : 0.0f);
        EXPECT_EQ(hero.headTargetZ, 0.0f);
    }
    world.Runtime().map = 0;
    world.Runtime().targets[4] = -1;
    scene.Step(input);
    EXPECT_EQ(hero.head.target.y, -9.0f);
    world.Runtime().warpCounter = 32;
    hero.head.target = {12.0f, 13.0f};
    hero.headTargetZ = 19.0f;
    scene.Step(input);
    EXPECT_EQ(hero.head.target.x, 12.0f);
    EXPECT_EQ(hero.head.target.y, 13.0f);
    EXPECT_EQ(hero.headTargetZ, 19.0f);
}

TEST(GameSceneTest, ConstructedMapEffectsReportMissingLiveModelMetadataAfterTheirPrefix)
{
    LoggedInSession online;
    FakeTextureStore textures;
    Client::Scenes::WorldState world(textures);
    world.Runtime().drawingEnabled = true;
    Client::Render::Models::ClothWorld cloth;
    MinimalPlayerLibrary models;
    Client::Scenes::GameData data;
    ASSERT_TRUE(world.Resources()->effects.Create(0x144, {}, {}, {}, 1.0f).has_value());
    GameScene scene(
        SceneContext{.dataRoot = {}, .textures = textures, .log = online.log, .cloth = &cloth, .world = &world},
        nullptr, &models, &data);
    scene.Step(Client::Input::InputState{});
    ASSERT_TRUE(scene.Failure().has_value());
    EXPECT_NE(scene.Failure()->find("metadata"), std::string::npos);
}

TEST(GameSceneTest, WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame)
{
    for (const std::uint8_t depth : std::array<std::uint8_t, 3>{0, 1, 255})
    {
        LoggedInSession online;
        Client::Scenes::WorldState world;
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpDepth = depth;
        world.Runtime().warpCounter = 31;
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(4567)};
        Client::Render::Models::CrtRandom expected(4567);
        FakeTextureStore textures;
        GameScene scene(
            SceneContext{.dataRoot = {}, .textures = textures, .log = online.log, .cloth = &cloth, .world = &world},
            nullptr);
        scene.PrepareFrame();
        if (depth == 0)
            for (int i = 0; i < 4; ++i)
                static_cast<void>(expected.Next());
        EXPECT_TRUE(scene.HoldsTheLastFrame());
        EXPECT_TRUE(scene.HoldsTheLastFrame());
        EXPECT_EQ(cloth.random.State(), expected.State());
        EXPECT_EQ(world.Runtime().warpDepth, depth);
        world.Runtime().warpCounter = 30;
        scene.PrepareFrame();
        EXPECT_FALSE(scene.HoldsTheLastFrame());
    }
}
} // namespace
