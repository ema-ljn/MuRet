#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/MapLoad.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/TerrainCells.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Client::Scenes::GameScene: la escena 5 mientras espera el mundo y con el mundo. Manda el F3 03 en su
// primer paso (0x636290), baja el contador de la espera como MoveHero (0x59af01) y retiene el frame
// mientras pase de 30 (0x6371a0); después dibuja hasta la primera etapa que MuRet no tiene y corta ahí.
// 06 § La entrada al mundo, § El paso del juego y § El dibujo del juego.
// Ningún test fija cuántos pasos corren durante la espera: los gates de MoveHero que MuRet no tiene
// todavía la cortan antes del contador (P-C28 b).
namespace
{
using Client::App::SessionState;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::WaitDrawThreshold;
using Client::Scenes::WorldLoadingWait;
using Client::Scenes::WorldRuntime;
using Client::Scenes::WorldState;
using Client::Tests::FakeTextureStore;
using Client::Tests::LoggedInSession;

constexpr std::uint32_t Seed = 0x13572468u;

// El rand() de la CRT sembrado con Seed después de `draws` llamadas.
CrtRandom RandomAfter(int draws)
{
    CrtRandom random(Seed);
    for (int i = 0; i < draws; ++i)
    {
        static_cast<void>(random.Next());
    }
    return random;
}

// Lo que deja un paso en el rand(): `draws` de la escena y, detrás, la tela (0x637700).
std::uint32_t StateAfterAStep(int draws)
{
    ClothWorld reference{.random = RandomAfter(draws)};
    Client::Render::Models::StepTheClothWorld(reference);
    return reference.random.State();
}

// Con el login, la lista y el lugar 3 elegido: la sesión espera la entrada.
void ChooseSlotThree(LoggedInSession& online)
{
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    online.session.RequestCharacterList();
    online.ReceiveTheList({{0, "char0001"}, {3, "char0002"}});
    online.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(online.session.ChosenCharacter(), nullptr);
}

// El WorldState, el rand() de la CRT y la escena, sin sesión salvo que se pase una.
struct Harness
{
    explicit Harness(Client::Core::Log& log, Client::App::ServerSession* session = nullptr)
        : scene(SceneContext{.dataRoot = {}, .textures = store, .log = log, .cloth = &cloth, .world = &world}, session)
    {
    }

    [[nodiscard]] WorldRuntime& Runtime()
    {
        return world.Runtime();
    }

    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    GameScene scene;
    const Client::Input::InputState input;
};

TEST(GameSceneTest, TheFirstStepSendsF303)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::EnteringWorld);
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("se ignora la entrada al mundo") != std::string::npos; }));
}

// Q-C70: en el juego (el modo 5), el FD_CLOSE no abre un aviso y el juego sigue sin conexión: el
// texto 3 en el área de mensajes es de la CL9. La escena no vuelve al login.
TEST(GameSceneTest, AFailedSessionKeepsTheGame)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);

    online.socket.peerClosed = true;
    static_cast<void>(online.session.Poll());
    tested.scene.Step(tested.input);

    EXPECT_EQ(online.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.scene.Next(), std::nullopt);
    EXPECT_FALSE(tested.scene.WantsToQuit());
}

// El original loguea el lugar y el nombre; MuRet, el lugar (el nombre es del jugador).
TEST(GameSceneTest, TheLogHasTheSlotNotTheName)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);

    EXPECT_TRUE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                    { return line.find("del lugar 3") != std::string::npos; }));
    EXPECT_FALSE(std::ranges::any_of(online.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}

// La espera vive en el WorldState y sus cierres gastan el rand() de la tela: sin alguno de los dos,
// el juego no entra en vez de dibujar sin esperar.
TEST(GameSceneTest, DoesNotEnterWithoutTheWorldOrTheRandom)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    GameScene withoutWorld(SceneContext{.dataRoot = {}, .textures = tested.store, .log = log, .cloth = &tested.cloth},
                           nullptr);
    GameScene withoutRandom(SceneContext{.dataRoot = {}, .textures = tested.store, .log = log, .world = &tested.world},
                            nullptr);

    EXPECT_FALSE(withoutWorld.Enter().HasValue());
    EXPECT_FALSE(withoutRandom.Enter().HasValue());
}

// 06, "La entrada al mundo", pasos 3, 4 y 6: con el mundo cargado y el contador que dejó la carga, el
// dibujo retiene; los pasos lo bajan sin llegar, y la respuesta del F3 03 lo pone en 30 (0x4c48eb),
// que ya no retiene.
TEST(GameSceneTest, HoldsTheFrameUntilTheWorldArrives)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WorldLoadingWait;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    EXPECT_TRUE(tested.scene.HoldsTheLastFrame()) << "antes del primer PrepareFrame retiene";

    for (int step = 0; step < 3; ++step)
    {
        tested.scene.Step(tested.input);
        tested.scene.PrepareFrame();
        EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    }
    EXPECT_GT(tested.Runtime().warpCounter, WaitDrawThreshold);

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();

    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
}

// 0x6374dd: con el mundo cargado y el contador en 30 o menos, el dibujo del juego llega a los personajes
// (0x510d00), y 0x50ba30 corre 0x505820 con cada jugador que ve (0x50c91e). MuRet no los dibuja: no sabe
// qué quedó en [0x743a178] y [0x743a17c]. Retenido, o sin el mundo cargado, no llega y quedan.
TEST(GameSceneTest, AFrameThatReachesTheCharactersForgetsTheLastScan)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().fullSetScan = Client::Scenes::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.PrepareFrame();
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    tested.scene.PrepareFrame();
    ASSERT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_TRUE(tested.Runtime().fullSetScan.has_value());

    tested.Runtime().warpCounter = WaitDrawThreshold;
    tested.scene.PrepareFrame();
    ASSERT_FALSE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().fullSetScan.has_value());
}

// 0x6371b7..0x637229: el dibujo lee el contador con un acceso que cierra (0x40b800) aunque después
// retenga; el cierre gasta 4 rand() solo si deja el acceso en 0. 0x63722e compara con signo: 31
// retiene y 30 no. Preguntar después no vuelve a leer.
TEST(GameSceneTest, WarpGateReadsAndClosesOnceEvenWhenItRetainsTheFrame)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        bool holds;
        int draws;
    };
    for (const Case& probe : {Case{31, 0, true, 4}, Case{31, 1, true, 0}, Case{31, 255, true, 0}, Case{30, 0, false, 4},
                              Case{30, 1, false, 0}, Case{30, 255, false, 0}, Case{-1, 0, false, 4}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.PrepareFrame();
        const bool holds = tested.scene.HoldsTheLastFrame();
        const bool again = tested.scene.HoldsTheLastFrame();

        EXPECT_EQ(holds, probe.holds);
        EXPECT_EQ(again, probe.holds);
        EXPECT_EQ(tested.Runtime().warpCounter, probe.counter);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        EXPECT_EQ(tested.cloth.random.State(), RandomAfter(probe.draws).State());
    }
}

// 0x59ae8d..0x59afd1: MoveHero baja el contador si es positivo en un acceso y lo vuelve a leer en
// otro; con los dos cerrando en 0 son 8 rand(), antes de los de la tela (0x637700). Los gates de
// antes (0x59ae24..0x59ae87) son frontera (P-C28 b). Con 30 o menos después de bajarlo, el paso sigue
// en los personajes (0x508f00), que sin el mapa cargado cortan en 0x508f14, sin la tela.
TEST(GameSceneTest, AStepLowersTheCounterWithTwoAccesses)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        std::int32_t after;
        int draws;
        bool cuts;
    };
    for (const Case& probe : {Case{WorldLoadingWait, 0, WorldLoadingWait - 1, 8, false}, Case{31, 1, 30, 0, true},
                              Case{1, 255, 0, 0, true}, Case{0, 0, 0, 8, true}, Case{-5, 0, -5, 8, true}})
    {
        SCOPED_TRACE(::testing::Message() << probe.counter << " con " << static_cast<int>(probe.depth));
        Client::Tests::RecordingSink sink;
        Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
        Harness tested(log);
        tested.Runtime().drawingEnabled = true;
        tested.Runtime().warpCounter = probe.counter;
        tested.Runtime().warpDepth = probe.depth;
        ASSERT_TRUE(tested.scene.Enter().HasValue());

        tested.scene.Step(tested.input);

        EXPECT_EQ(tested.Runtime().warpCounter, probe.after);
        EXPECT_EQ(tested.Runtime().warpDepth, probe.depth);
        if (probe.cuts)
        {
            ASSERT_TRUE(tested.scene.Failure().has_value());
            EXPECT_NE(tested.scene.Failure()->find("0x508f14"), std::string::npos) << *tested.scene.Failure();
            EXPECT_EQ(tested.cloth.random.State(), RandomAfter(probe.draws).State());
        }
        else
        {
            EXPECT_EQ(tested.scene.Failure(), std::nullopt);
            EXPECT_EQ(tested.cloth.random.State(), StateAfterAStep(probe.draws));
        }
    }
}

// 0x636da3 y 0x6371a6: sin el mundo cargado y sin el estado 0x3d, el paso sale antes de MoveHero y el
// dibujo devuelve 0 sin leer el contador. La tela corre igual.
TEST(GameSceneTest, WithoutTheLoadedWorldNeitherTheStepNorTheDrawReadTheCounter)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().warpCounter = 30;
    ASSERT_TRUE(tested.scene.Enter().HasValue());

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.scene.HoldsTheLastFrame());
    EXPECT_FALSE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_EQ(tested.cloth.random.State(), StateAfterAStep(0));
}

// 0x636da3..0x636dac: con la sesión en el mundo (el 0x3d que publica la respuesta del F3 03), el paso
// marca el mundo cargado aunque el F1 02 lo hubiera bajado, y sigue hasta MoveHero.
TEST(GameSceneTest, TheWorldStateTurnsTheDrawingOn)
{
    LoggedInSession online;
    ASSERT_NO_FATAL_FAILURE(ChooseSlotThree(online));
    Harness tested(online.log, &online.session);
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    tested.scene.Step(tested.input);
    ASSERT_EQ(online.session.State(), SessionState::EnteringWorld);
    online.eventSink.onWorld = [](const Client::App::WorldEvent&,
                                  Client::App::ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    // Una respuesta sintética de 0x34 bytes: la escena no mira sus campos.
    Client::Network::PacketSerials serials;
    online.socket.reads = {Client::Tests::FromTheGameServer(
        Client::Tests::Bytes({0xC1, 0x34, 0xF3, 0x03, 0x10, 0x20, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                              0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12,
                              0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
                              0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x00, 0x00, 0x26, 0x27, 0x28, 0x29, 0x2A}),
        serials)};
    ASSERT_TRUE(online.session.Poll().HasValue());
    ASSERT_EQ(online.session.State(), SessionState::InWorld);
    tested.Runtime().drawingEnabled = false;
    tested.Runtime().warpCounter = 31;

    tested.scene.Step(tested.input);
    tested.scene.PrepareFrame();

    EXPECT_TRUE(tested.Runtime().drawingEnabled);
    EXPECT_EQ(tested.Runtime().warpCounter, 30);
    EXPECT_FALSE(tested.scene.HoldsTheLastFrame());
    ASSERT_TRUE(tested.scene.Failure().has_value()) << "sin el mapa cargado, los personajes cortan";
    EXPECT_NE(tested.scene.Failure()->find("0x508f14"), std::string::npos) << *tested.scene.Failure();
}

// 0x636dc2..0x636ddb: con el mundo cargado, cada paso deja el temblor de la cámara en su 0,2 (0x6b8854),
// en float; sin el mundo cargado el paso sale antes y no lo toca.
TEST(GameSceneTest, AStepWithTheWorldLeavesAFifthOfTheShake)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    Client::World::GameCameraInput cameraInput;
    GameScene scene(SceneContext{.dataRoot = {},
                                 .textures = store,
                                 .log = log,
                                 .cloth = &cloth,
                                 .cameraInput = &cameraInput,
                                 .world = &world},
                    nullptr);
    ASSERT_TRUE(scene.Enter().HasValue());
    cameraInput.shake = 3.0f;
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, 3.0f) << "sin el mundo cargado, 0x636dbc sale antes";

    world.Runtime().drawingEnabled = true;
    world.Runtime().warpCounter = WorldLoadingWait;
    const auto decay = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    scene.Step(Client::Input::InputState{});
    const auto once = static_cast<float>(3.0 * decay);
    EXPECT_EQ(cameraInput.shake, once);
    scene.Step(Client::Input::InputState{});
    EXPECT_EQ(cameraInput.shake, static_cast<float>(static_cast<double>(once) * decay));
}

// Con el frame retenido el dibujo no dibuja ni corta: solo queda el cierre de 0x637229.
TEST(GameSceneTest, AHeldFrameDrawsNothing)
{
    Client::Tests::RecordingSink sink;
    Client::Core::Log log(sink, Client::Core::LogLevel::Debug);
    Harness tested(log);
    tested.Runtime().drawingEnabled = true;
    tested.Runtime().warpCounter = WaitDrawThreshold + 1;
    ASSERT_TRUE(tested.scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;

    tested.scene.PrepareFrame();
    tested.scene.Render(target.BeginFrame(Client::Core::Duration{}), 0.0f);
    target.Present();

    ASSERT_EQ(target.frames.size(), 1u);
    EXPECT_TRUE(target.frames[0].events.empty());
    EXPECT_EQ(tested.scene.Failure(), std::nullopt);
    EXPECT_FALSE(tested.scene.MouseRay().has_value());
    EXPECT_EQ(tested.cloth.random.State(), RandomAfter(4).State());
}

// El dibujo del juego con un mapa cargado de la Data del repo (0x61a560, MapLoad) y el héroe publicado
// en su celda, sin pasar por el F3 03.
class GameSceneDrawTest : public Client::Tests::GoldenDataTest
{
protected:
    // El mapa `map` cargado en el juego y el héroe en el lugar HeroSlot, en la celda (x, y), publicado,
    // con la cámara de arranque y el rand() en la semilla.
    void Prepare(std::uint8_t map, std::uint8_t x, std::uint8_t y)
    {
        world.Runtime().map = map;
        const auto loaded = Client::Scenes::LoadMap(map, Client::Scenes::MapLoadContext{.dataRoot = m_dataRoot,
                                                                                        .world = world,
                                                                                        .scene = SceneId::Game,
                                                                                        .playerKey = 5,
                                                                                        .random = cloth.random});
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        Client::Scenes::ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.cellX = x;
        hero.cellY = y;
        hero.position = {(static_cast<float>(x) * 100.0f) + 50.0f, (static_cast<float>(y) * 100.0f) + 50.0f, 165.0f};
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.Runtime().hero = HeroSlot;
        world.Runtime().drawingEnabled = true;
        world.Runtime().warpCounter = WaitDrawThreshold;
        camera = Client::World::GameCamera{};
        cameraInput = Client::World::GameCameraInput{};
        cloth.random = CrtRandom(Seed);
    }

    [[nodiscard]] GameScene Scene()
    {
        return GameScene(SceneContext{.dataRoot = m_dataRoot,
                                      .textures = store,
                                      .log = log,
                                      .cloth = &cloth,
                                      .camera = &camera,
                                      .cameraInput = &cameraInput,
                                      .world = &world},
                         nullptr);
    }

    static constexpr std::size_t HeroSlot = 42;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    FakeTextureStore store;
    ClothWorld cloth{.random = CrtRandom(Seed)};
    WorldState world;
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
};

// El fin del pase de los modelos del frame, si lo hubo (uno solo).
const Client::Tests::FrameEvent* ModelsOf(const Client::Tests::RecordedFrame& frame)
{
    const Client::Tests::FrameEvent* models = nullptr;
    for (const Client::Tests::FrameEvent& event : frame.events)
    {
        if (event.kind == Client::Tests::FrameEvent::Kind::Models)
        {
            EXPECT_EQ(models, nullptr) << "un solo pase de modelos";
            models = &event;
        }
    }
    return models;
}

// 0x63723a..0x6374c6 en Lorencia (el 0) y Devias (el 2), los mapas de referencia: la cámara con los dos
// accesos a la celda del héroe (8 rand() después de los 4 de 0x637229), el rayo del mouse, el primer
// pase del terreno (las celdas, con los tiles de 0x23 a 0x30 y el 0x122) y el corte en el pase del pasto
// (0x5fc353), que MuRet no tiene. El frame cortado no se presenta y el dibujo no vuelve a correr.
TEST_F(GameSceneDrawTest, TheReferenceMapsDrawTheTerrainAndStopAtTheGrass)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& probe : {Case{0, 143, 120}, Case{2, 220, 30}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;

        scene.PrepareFrame();
        ASSERT_FALSE(scene.HoldsTheLastFrame());
        scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);

        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find("0x5fc353"), std::string::npos) << *scene.Failure();
        ASSERT_EQ(target.frames.size(), 1u);
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
        ASSERT_NE(models, nullptr);
        EXPECT_FALSE(models->modelVertices.empty());
        for (const Client::Render::ModelDrawCommand& command : models->modelCommands)
        {
            ASSERT_TRUE(command.state.texture.has_value());
            const std::uint16_t slot = command.state.texture->value;
            EXPECT_TRUE((slot >= Client::World::FirstTileSlot && slot < Client::World::FirstTileSlot + 14) ||
                        slot == 0x122)
                << slot;
        }
        EXPECT_TRUE(scene.MouseRay().has_value());
        EXPECT_EQ(cameraInput.hero, world.Actors()[HeroSlot].position);
        EXPECT_EQ(cloth.random.State(), RandomAfter(4 + 8).State());
        EXPECT_NE(std::ranges::find(store.loaded, Client::World::FirstTileSlot), store.loaded.end());
        EXPECT_NE(std::ranges::find(store.loaded, std::uint16_t{0x122}), store.loaded.end());

        target.Present();
        scene.Render(target.BeginFrame({}), 0.0f);
        target.Present();
        EXPECT_TRUE(target.frames.back().events.empty()) << "cortado, no vuelve a dibujar";
        EXPECT_EQ(cloth.random.State(), RandomAfter(4 + 8).State());
        scene.Leave();
        EXPECT_EQ(store.Size(), 0u) << "Leave suelta los tiles";
    }
}

// Los otros mapas que carga el juego cortan antes del pasto: el 6 en la cámara (0x576960 con el nombre
// del héroe), antes de los accesos; el 7 en su agua (0x5fa4b0) y el 10, que no tiene terreno, en los
// objetos (0x6046a0), los dos ya en el pase del mundo y sin una celda.
TEST_F(GameSceneDrawTest, TheOtherMapsStopWhereTheyDiffer)
{
    struct Case
    {
        std::uint8_t map;
        std::string_view frontier;
        int draws;
        bool worldPass;
    };
    for (const Case& probe :
         {Case{6, "0x635b5c", 4, false}, Case{7, "0x5fa985", 4 + 8, true}, Case{10, "0x6374ce", 4 + 8, true}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, 128, 128));
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        Client::Tests::RecordingFrameTarget target;

        scene.PrepareFrame();
        scene.Render(target.BeginFrame({}), 0.0f);

        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find(probe.frontier), std::string::npos) << *scene.Failure();
        const Client::Tests::FrameEvent* models = ModelsOf(target.frames[0]);
        EXPECT_EQ(models != nullptr, probe.worldPass);
        if (models != nullptr)
        {
            EXPECT_TRUE(models->modelVertices.empty());
        }
        EXPECT_EQ(scene.MouseRay().has_value(), probe.worldPass);
        EXPECT_EQ(cloth.random.State(), RandomAfter(probe.draws).State());
        scene.Leave();
    }
}

// 0x63710f: el primer paso después de la espera llega a los personajes (0x508f00) antes del primer dibujo del
// juego. El TerrainWall pierde el 0x02 (el de (221, 11) en Lorencia, Q-C170) y 0x5fba90 lee el trapecio que
// dejó el último dibujo de la selección, que MuRet no calcula: corta en el lugar 0, muerto, sin gastar más que
// los 8 rand() de MoveHero y sin la tela. Después del corte, el paso no vuelve a correr.
TEST_F(GameSceneDrawTest, TheFirstStepAfterTheWaitStopsAtTheSelectionTrapezoid)
{
    struct Case
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
    };
    for (const Case& probe : {Case{0, 143, 120}, Case{2, 220, 30}})
    {
        SCOPED_TRACE(static_cast<int>(probe.map));
        ASSERT_NO_FATAL_FAILURE(Prepare(probe.map, probe.x, probe.y));
        GameScene scene = Scene();
        ASSERT_TRUE(scene.Enter().HasValue());
        const std::size_t wall = Client::Data::Terrain::CellIndex(221, 11);
        const std::uint16_t before = world.Map().terrain.attributes->cells[wall];

        scene.Step(Client::Input::InputState{});

        ASSERT_TRUE(scene.Failure().has_value());
        EXPECT_NE(scene.Failure()->find("0x5fba90"), std::string::npos) << *scene.Failure();
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1);
        EXPECT_EQ(world.Map().terrain.attributes->cells[wall], before & ~0x02);
        if (probe.map == 0)
        {
            EXPECT_EQ(before, 2);
        }
        EXPECT_EQ(cloth.random.State(), RandomAfter(8).State());

        scene.Step(Client::Input::InputState{});
        EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold - 1) << "cortado, el paso no vuelve a correr";
        EXPECT_EQ(cloth.random.State(), RandomAfter(8).State());
        scene.Leave();
    }
}

// Un dibujo cortado deja la escena en Failure: el paso que sigue no corre, aunque el dibujo haya dejado su
// trapecio.
TEST_F(GameSceneDrawTest, AStepAfterACutDrawDoesNothing)
{
    ASSERT_NO_FATAL_FAILURE(Prepare(0, 143, 120));
    GameScene scene = Scene();
    ASSERT_TRUE(scene.Enter().HasValue());
    Client::Tests::RecordingFrameTarget target;
    scene.PrepareFrame();
    scene.Render(target.BeginFrame(std::chrono::milliseconds{1000}), 0.0f);
    ASSERT_TRUE(scene.Failure().has_value());
    const std::string cut = *scene.Failure();
    const std::uint32_t random = cloth.random.State();

    scene.Step(Client::Input::InputState{});

    EXPECT_EQ(scene.Failure(), cut);
    EXPECT_EQ(world.Runtime().warpCounter, WaitDrawThreshold);
    EXPECT_EQ(cloth.random.State(), random);
    scene.Leave();
}
} // namespace
