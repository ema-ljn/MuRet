#include "Scenes/GameScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/PacketSerials.h"
#include "Render/Models/Cloth.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

// Client::Scenes::GameScene: la escena 5 mientras espera el mundo. Manda el F3 03 en su primer paso
// (0x636290), baja el contador de la espera como MoveHero (0x59af01) y retiene el frame mientras pase
// de 30 (0x6371a0). 06 § La entrada al mundo, § El paso del juego y § El dibujo del juego.
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
// antes (0x59ae24..0x59ae87) son frontera (P-C28 b).
TEST(GameSceneTest, AStepLowersTheCounterWithTwoAccesses)
{
    struct Case
    {
        std::int32_t counter;
        std::uint8_t depth;
        std::int32_t after;
        int draws;
    };
    for (const Case& probe : {Case{WorldLoadingWait, 0, WorldLoadingWait - 1, 8}, Case{31, 1, 30, 0},
                              Case{1, 255, 0, 0}, Case{0, 0, 0, 8}, Case{-5, 0, -5, 8}})
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
        EXPECT_EQ(tested.cloth.random.State(), StateAfterAStep(probe.draws));
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
}
} // namespace
