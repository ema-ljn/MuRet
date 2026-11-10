#include "Scenes/LoginScene.h"
#include "Render/Pass3D.h"
#include "Scenes/SceneCameras.h"

#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/WorldFolder.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/QuadCorner.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/TextureSets.h"
#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FakeTextureStore.h"
#include "support/GateFiles.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/SkillFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"
#include "support/UiInput.h"
#include "UI/GameCursor.h"
#include "UI/Widget.h"
#include "Scenes/LoginModels.h"
#include "support/FixedRandom.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::LoginScene: la escena 2 sin que la sesión pida una ventana. Entra como 0x6349a0
// (sus texturas, las de las ventanas y el ConnectServer) y dibuja como 0x6350b0. Con un Data\ que
// es sintético, la tabla de texturas falsa y el RecordingFrameTarget, que anota cada quad, cada
// texto, el fin del 2D y el pase 3D en orden. Su UI con la sesión (CL5.5) está en
// test_login_scene_session.cpp. Los textos son inventados: los de Text.bmd no van a los tests.
namespace
{
using Client::App::PixelSize;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Network::Connection;
using Client::Network::IdentityObfuscation;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::FanCorners;
using Client::Render::OriginalQuad;
using Client::Render::OriginalSkewedQuad;
using Client::Render::QuadCorners;
using Client::Render::SkewedFanCorners;
using Client::Render::TextureSlot;
using Client::Render::Text::BottomAlignedAt;
using Client::Render::Text::FontKind;
using Client::Render::Text::RightAlignedAt;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextExtent;
using Client::Render::Text::TextStyle;
using Client::Scenes::GameData;
using Client::Scenes::LoadGameData;
using Client::Scenes::LoginLeftText;
using Client::Scenes::LoginRightText;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::LoginVersionText;
using Client::Scenes::PlaceVersion;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::WindowTextureSet;
using Client::Scenes::WorldOf;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Client::Tests::GameClientKeys;
using Client::Tests::RecordedText;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextTable;
using Client::Tests::WriteTextureSet;
using Client::UI::CursorQuad;
using Client::UI::Point;

using Kind = FrameEvent::Kind;

// Textos inventados en los índices del login, y una versión cualquiera.
constexpr std::string_view LeftText = "texto de la izquierda";
constexpr std::string_view RightText = "texto de la derecha";
constexpr std::string_view VersionFormat = "prueba %s fin";
constexpr std::string_view Version = "9.8.7";
constexpr std::string_view VersionPlaced = "prueba 9.8.7 fin";

// El estilo de los tres textos: la fuente DAT_05763554, el texto en blanco (DAT_006c8e5c) y el
// fondo en 0x80000000 (DAT_006c8e64).
constexpr TextStyle LoginTextStyle{FontKind::Normal, TextColors{0xffffffff, 0x80000000}};

// Los colores de 0x6350b0: el blanco de los fondos, glColor3f(0,25; 0,1; 0,1) de la arena y
// glColor3f(0,8; 0,7; 0,6) de los textos.
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color SandColor{0.25f, 0.1f, 0.1f, 1.0f};
constexpr Color TextColor{0.8f, 0.7f, 0.6f, 1.0f};

// El reloj de los frames de los tests que no miran la arena.
constexpr std::chrono::milliseconds AnyClock{40};

std::map<std::size_t, std::string> LoginTexts()
{
    return {{LoginLeftText, std::string(LeftText)},
            {LoginRightText, std::string(RightText)},
            {LoginVersionText, std::string(VersionFormat)}};
}

QuadCorners CornersOf(const FrameEvent& event)
{
    QuadCorners corners{};
    constexpr std::array<std::size_t, 4> fan = {0, 1, 2, 5};
    for (std::size_t i = 0; i < fan.size(); ++i)
    {
        const auto& vertex = event.vertices[fan[i]];
        corners[i] = {vertex.x, vertex.y, vertex.u, vertex.v};
    }
    return corners;
}

void ExpectSameCorners(const QuadCorners& actual, const QuadCorners& expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(actual[i].x, expected[i].x);
        EXPECT_EQ(actual[i].y, expected[i].y);
        EXPECT_EQ(actual[i].u, expected[i].u);
        EXPECT_EQ(actual[i].v, expected[i].v);
    }
}

void ExpectColor(const FrameEvent& event, const Color& color)
{
    for (const auto& vertex : event.vertices)
    {
        EXPECT_EQ(vertex.color, color);
    }
}

// Los fondos de 0x6350b0: 0x621b60(slot, x, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0).
OriginalQuad Background(std::uint16_t slot, float x)
{
    return OriginalQuad{.slot = TextureSlot{slot},
                        .x = x,
                        .y = 25.0f,
                        .width = 320.0f,
                        .height = 320.0f,
                        .scaleSize = true,
                        .scalePosition = true};
}

// La u de una arena: ((int) T % 100000) · velocidad, con la cuenta en la x87 y guardada en float.
float SandU(std::chrono::milliseconds clock, float speed)
{
    return static_cast<float>(static_cast<double>(clock.count() % 100000) * static_cast<double>(speed));
}

OriginalSkewedQuad Sand(std::uint16_t slot, float u, float du, float dv)
{
    return OriginalSkewedQuad{.slot = TextureSlot{slot},
                              .x = 0.0f,
                              .y = 25.0f,
                              .width = 640.0f,
                              .height = 430.0f,
                              .u = u,
                              .v = 0.0f,
                              .du = du,
                              .dv = dv};
}

// Lo que mide un texto en el RecordingTextDrawer.
TextExtent ExtentOf(std::string_view text)
{
    return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
}

struct Harness
{
    explicit Harness(PixelSize screen = {640, 480}, const std::map<std::size_t, std::string>& texts = LoginTexts())
        : log(sink, LogLevel::Debug), target(screen)
    {
        WriteTextureSet(folder.Path(), LoginTextureSet);
        WriteTextureSet(folder.Path(), WindowTextureSet);
        WriteTextTable(folder.Path(), texts);
        Client::Tests::WriteSceneFile(folder.Path(), "Gate.bmd", Client::Tests::GateTableBytes({}));
        Client::Tests::WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
        Client::Tests::WriteEmptyItemSetFiles(folder.Path());
        auto loaded = LoadGameData(folder.Path());
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
        {
            data = std::move(loaded).Value();
        }
    }

    // La escena, con la sesión o sin ella.
    LoginScene& Make(ServerSession* session = nullptr)
    {
        return scene.emplace(SceneContext{folder.Path(), store, log}, data, models, session, Version, FixedSeed);
    }

    // Y con el mouse de `input` en el contexto, que el cursor lee en el dibujo.
    LoginScene& MakeWithTheMouse()
    {
        return scene.emplace(SceneContext{.dataRoot = folder.Path(), .textures = store, .log = log, .input = &input},
                             data, models, nullptr, Version, FixedSeed);
    }

    // Un frame con el reloj `clock`. Devuelve lo que se dibujó.
    std::vector<FrameEvent> Frame(std::chrono::milliseconds clock = AnyClock, float alpha = 0.0f)
    {
        Client::Scenes::SceneCanvas& canvas = target.BeginFrame(clock);
        scene->Render(canvas, alpha);
        target.Present();
        return target.frames.back().events;
    }

    TemporaryFolder folder{"login-scene"};
    FakeTextureStore store;
    RecordingSink sink;
    Log log;
    RecordingFrameTarget target;
    GameData data;
    Client::Scenes::SingleModelLibrary models;
    std::optional<LoginScene> scene;
    InputState input;
};

// Una sesión que no conectó todavía, sobre un FakeSocket.
struct Session
{
    Session()
        : connection(socket, IdentityObfuscation::Shared(), clock, log, 1),
          session(connection, clock, log, GameClientKeys(), SessionSettings{ServerAddress{"10.0.0.1", 44405}},
                  eventSink)
    {
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session;
};

std::vector<Kind> KindsOf(const std::vector<FrameEvent>& events)
{
    std::vector<Kind> kinds;
    for (const FrameEvent& event : events)
    {
        kinds.push_back(event.kind);
    }
    return kinds;
}

// 0x6350b0: los dos fondos, el fin del 2D, el pase 3D, las dos arenas y los tres textos. Dos
// frames: el segundo empieza con la mezcla aditiva que dejó el primero, y los fondos van igual
// con la normal.
TEST(LoginSceneTest, DrawsInTheOrderOfTheOriginal)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());
    const std::vector<Kind> expected = {Kind::Quad, Kind::Quad, Kind::Flush, Kind::Models, Kind::Quad,
                                        Kind::Quad, Kind::Text, Kind::Text,  Kind::Text};

    for (int frame = 0; frame < 2; ++frame)
    {
        SCOPED_TRACE(frame);
        const std::vector<FrameEvent> events = tested.Frame();

        ASSERT_EQ(KindsOf(events), expected);
        EXPECT_EQ(events[0].texture, TextureSlot{0x15});
        EXPECT_EQ(events[1].texture, TextureSlot{0x16});
        EXPECT_EQ(events[4].texture, TextureSlot{0x5c2});
        EXPECT_EQ(events[5].texture, TextureSlot{0x5c3});
        for (const std::size_t background : {0u, 1u})
        {
            EXPECT_EQ(events[background].blend, BlendMode::Alpha);
            EXPECT_TRUE(events[background].alphaTest);
            ExpectColor(events[background], White);
        }
        for (const std::size_t sand : {4u, 5u})
        {
            EXPECT_EQ(events[sand].blend, BlendMode::Additive);
            EXPECT_FALSE(events[sand].alphaTest);
            ExpectColor(events[sand], SandColor);
        }
        // 0x554000 no cambia la mezcla: los textos salen aditivos, con el color de GL.
        for (const std::size_t text : {6u, 7u, 8u})
        {
            EXPECT_EQ(events[text].blend, BlendMode::Additive);
            ExpectColor(events[text], TextColor);
        }
        EXPECT_EQ(events[6].text.text, LeftText);
        EXPECT_EQ(events[7].text.text, RightText);
        EXPECT_EQ(events[8].text.text, VersionPlaced);
    }
}

// 0x635560: lo último del frame, después de la UI, es el cursor del juego (0x5ac2d0) en el mouse del
// WndProc, con la mezcla normal y en blanco aunque los textos dejaron la aditiva; con el botón abajo, el
// CursorPush. Sin el mouse en el contexto (los demás tests), no hay cursor.
TEST(LoginSceneTest, DrawsTheGameCursorLast)
{
    constexpr PixelSize Screen{1024, 768};
    Harness tested(Screen);
    ASSERT_TRUE(tested.MakeWithTheMouse().Enter().HasValue());
    Client::Tests::MoveMouse(tested.input, 512, 384);

    std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 10u);
    const FrameEvent& cursor = events.back();
    EXPECT_EQ(cursor.kind, Kind::Quad);
    EXPECT_EQ(cursor.texture, TextureSlot{2});
    EXPECT_EQ(cursor.blend, BlendMode::Alpha);
    EXPECT_TRUE(cursor.alphaTest);
    ExpectColor(cursor, White);
    ExpectSameCorners(CornersOf(cursor), FanCorners(CursorQuad(Point{320, 240}, false), Screen));

    Client::Tests::PressLeft(tested.input);
    events = tested.Frame();
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().texture, TextureSlot{3});
}

// Los fondos y las arenas con los argumentos de 0x6350b0, en 640 × 480 y en otra pantalla.
TEST(LoginSceneTest, UsesTheQuadsOfTheOriginal)
{
    constexpr std::chrono::milliseconds clock{123456};
    for (const PixelSize screen : {PixelSize{640, 480}, PixelSize{1024, 768}, PixelSize{1280, 1024}})
    {
        SCOPED_TRACE(screen.width);
        Harness tested(screen);
        ASSERT_TRUE(tested.Make().Enter().HasValue());

        const std::vector<FrameEvent> events = tested.Frame(clock);

        ASSERT_EQ(events.size(), 9u);
        ExpectSameCorners(CornersOf(events[0]), FanCorners(Background(0x15, 0.0f), screen));
        ExpectSameCorners(CornersOf(events[1]), FanCorners(Background(0x16, 320.0f), screen));
        ExpectSameCorners(CornersOf(events[4]),
                          SkewedFanCorners(Sand(0x5c2, SandU(clock, 0.0001f), 0.3f, 0.3f), screen));
        ExpectSameCorners(CornersOf(events[5]),
                          SkewedFanCorners(Sand(0x5c3, SandU(clock, 0.0005f), 3.0f, 2.0f), screen));
    }
}

// El mundo de la escena es el de su tabla, el 55 (World56): sin esa carpeta, como en esta, el login
// va sin terreno, lo dice en el log y dibuja lo demás.
TEST(LoginSceneTest, WithoutWorld56TheLoginStillDraws)
{
    Harness tested;
    ASSERT_TRUE(WorldOf(SceneId::Login).has_value());
    EXPECT_EQ(Client::Data::WorldFolder(WorldOf(SceneId::Login)->map), "World56");
    ASSERT_TRUE(tested.Make().Enter().HasValue());

    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(events.size(), 9u);
    EXPECT_EQ(events[3].kind, Kind::Models);
    bool warned = false;
    for (const auto& line : tested.sink.lines)
    {
        warned =
            warned || line.find("el login va sin el terreno: Data\\World56\\EncTerrain56.map") != std::string::npos;
    }
    EXPECT_TRUE(warned);
}

// El pase 3D va con la cámara fija de la escena 2 (0x635ae0) en el viewport de 0x620db0(0, 25, 640,
// 430): en 640 × 480, la y de GL es 480 − 25 − 430 (CL6.3).
TEST(LoginSceneTest, TheModelsGetThePassOfTheLoginCamera)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());

    const std::vector<FrameEvent> events = tested.Frame(AnyClock, 0.25f);

    ASSERT_EQ(events.size(), 9u);
    ASSERT_EQ(events[3].kind, Kind::Models);
    ASSERT_TRUE(events[3].pass.has_value());
    const Client::Render::Pass3D expected = Client::Render::MakePass3D(
        Client::App::PixelSize{640, 480}, Client::Scenes::WorldViewport, Client::Scenes::LoginCamera());
    EXPECT_EQ(events[3].pass->viewport, glm::ivec4(0, 25, 640, 430));
    EXPECT_EQ(events[3].pass->projection, expected.projection);
    EXPECT_EQ(events[3].pass->view, expected.view);
}

// La u de las arenas sale del reloj del frame (Q-C65): otro reloj, otra u; 100 s después, la
// misma, porque la cuenta es módulo 100.000 ms.
TEST(LoginSceneTest, TheSandRunsWithTheFrameClock)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());

    const QuadCorners first = CornersOf(tested.Frame(std::chrono::milliseconds{1234})[4]);
    const QuadCorners later = CornersOf(tested.Frame(std::chrono::milliseconds{5678})[4]);
    const QuadCorners wrapped = CornersOf(tested.Frame(std::chrono::milliseconds{101234})[4]);
    const QuadCorners fast = CornersOf(tested.Frame(std::chrono::milliseconds{1234})[5]);

    EXPECT_EQ(first[0].u, SandU(std::chrono::milliseconds{1234}, 0.0001f));
    EXPECT_NE(later[0].u, first[0].u);
    EXPECT_EQ(later[0].u, SandU(std::chrono::milliseconds{5678}, 0.0001f));
    EXPECT_EQ(wrapped[0].u, first[0].u);
    EXPECT_EQ(fast[0].u, SandU(std::chrono::milliseconds{1234}, 0.0005f));
}

// Apoyados en la y 479, con la división entera de TextLayout: el 454 termina en x = 335, el 455
// empieza ahí y el de la versión va en x = 0.
TEST(LoginSceneTest, PlacesTheTextsLikeTheOriginal)
{
    for (const PixelSize screen : {PixelSize{640, 480}, PixelSize{1280, 1024}})
    {
        SCOPED_TRACE(screen.width);
        Harness tested(screen);
        ASSERT_TRUE(tested.Make().Enter().HasValue());

        tested.Frame();

        const std::vector<RecordedText>& texts = tested.target.Texts();
        ASSERT_EQ(texts.size(), 3u);
        EXPECT_EQ(texts[0].x, RightAlignedAt(335, ExtentOf(LeftText), screen));
        EXPECT_EQ(texts[0].y, BottomAlignedAt(479, ExtentOf(LeftText), screen));
        EXPECT_EQ(texts[1].x, 335);
        EXPECT_EQ(texts[1].y, BottomAlignedAt(479, ExtentOf(RightText), screen));
        EXPECT_EQ(texts[2].x, 0);
        EXPECT_EQ(texts[2].y, BottomAlignedAt(479, ExtentOf(VersionPlaced), screen));
        for (const RecordedText& text : texts)
        {
            EXPECT_EQ(text.style, LoginTextStyle);
        }
    }
}

// El 456 con la versión de MuRet donde la marca el texto (Q-C61).
TEST(LoginSceneTest, ShowsTheVersionOfMuRet)
{
    Harness tested;
    ASSERT_TRUE(tested.Make().Enter().HasValue());

    tested.Frame();

    ASSERT_EQ(tested.target.Texts().size(), 3u);
    EXPECT_EQ(tested.target.Texts()[2].text, VersionPlaced);
}

// Solo el primer "%s"; el resto del texto va tal cual, y sin "%s" el texto queda como está:
// nunca es un formato.
TEST(LoginSceneTest, PlacesTheVersionOnlyWhereTheTextMarksIt)
{
    EXPECT_EQ(PlaceVersion("a %s b", "1.2"), "a 1.2 b");
    EXPECT_EQ(PlaceVersion("%s", "1.2"), "1.2");
    EXPECT_EQ(PlaceVersion("%s y %s", "1.2"), "1.2 y %s");
    EXPECT_EQ(PlaceVersion("%d %s %n", "1.2"), "%d 1.2 %n");
    EXPECT_EQ(PlaceVersion("sin marca", "1.2"), "sin marca");
    EXPECT_EQ(PlaceVersion("", "1.2"), "");
}

// 0x6349a0: las texturas, las suyas y las de las ventanas, y después el ConnectServer (0x4c1340).
TEST(LoginSceneTest, StartsTheSessionOnEnter)
{
    Harness tested;
    Session online;
    LoginScene& scene = tested.Make(&online.session);
    ASSERT_EQ(online.session.State(), SessionState::Idle);

    ASSERT_TRUE(scene.Enter().HasValue());

    EXPECT_EQ(online.session.State(), SessionState::ConnectingConnectServer);
    ASSERT_EQ(online.socket.connects.size(), 1u);
    EXPECT_EQ(online.socket.connects[0].host, "10.0.0.1");
    EXPECT_EQ(online.socket.connects[0].port, 44405);
    EXPECT_EQ(tested.store.Size(), LoginTextureSet.size() + WindowTextureSet.size());
    EXPECT_EQ(scene.Next(), std::nullopt);
    EXPECT_FALSE(scene.WantsToQuit());
    EXPECT_FALSE(scene.WantsTextInput());
}

// Sin servidor (Main sin host), la escena dibuja igual y no pide la selección.
TEST(LoginSceneTest, WithoutASessionItDrawsAndDoesNotConnect)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());

    for (int step = 0; step < 100; ++step)
    {
        scene.Step(tested.input);
    }

    EXPECT_EQ(scene.Next(), std::nullopt);
    EXPECT_FALSE(scene.HoldsTheLastFrame());
    EXPECT_EQ(tested.Frame().size(), 9u);
}

// 0x6350b0 no dibuja ni presenta mientras 0x7d3a13d esté en 0 (0x6350b6..0x6350c4), y la prende el paso
// que entra (0x634c87): con conexión o sin ella, el login no se ve antes de su primer paso. Sin sesión, la
// escena retiene el frame hasta ese paso; así las brasas del primero nacen alrededor de (0, 0, 0) (Q-C137).
TEST(LoginSceneTest, WithoutASessionItHoldsTheFrameUntilItsFirstStep)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_TRUE(scene.HoldsTheLastFrame());

    scene.Step(tested.input);
    EXPECT_FALSE(scene.HoldsTheLastFrame());

    ASSERT_TRUE(scene.Enter().HasValue());
    EXPECT_TRUE(scene.HoldsTheLastFrame());
}

// La sesión empezada no alcanza: la selección es con el login aceptado (LoggedIn). Lo prueba de
// punta a punta test_login_scene_loopback.cpp.
TEST(LoginSceneTest, StaysWhileTheSessionIsNotLoggedIn)
{
    Harness tested;
    Session online;
    LoginScene& scene = tested.Make(&online.session);
    ASSERT_TRUE(scene.Enter().HasValue());
    static_cast<void>(online.session.Poll());

    scene.Step(tested.input);

    EXPECT_NE(online.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(scene.Next(), std::nullopt);
}

// Una textura que falta corta antes de subir nada y de conectar.
TEST(LoginSceneTest, AMissingTextureFailsWithItsName)
{
    Harness tested;
    std::filesystem::remove(tested.folder.Path() / "Logo/Login_Back02.OZJ");
    Session online;
    LoginScene& scene = tested.Make(&online.session);

    const auto entered = scene.Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("Logo\\Login_Back02.jpg"), std::string::npos) << entered.Error();
    EXPECT_EQ(tested.store.Size(), 0u);
    EXPECT_EQ(online.session.State(), SessionState::Idle);
    EXPECT_TRUE(online.socket.connects.empty());
}

// Una de las ventanas que falta también corta antes de conectar. Lo que subió se suelta con el
// Leave, como hace el SceneManager con un Enter que falla.
TEST(LoginSceneTest, AMissingWindowTextureFailsWithItsName)
{
    Harness tested;
    std::filesystem::remove(tested.folder.Path() / "Interface/message_back.OZT");
    Session online;
    LoginScene& scene = tested.Make(&online.session);

    const auto entered = scene.Enter();

    ASSERT_FALSE(entered.HasValue());
    EXPECT_NE(entered.Error().find("Interface\\message_back.tga"), std::string::npos) << entered.Error();
    EXPECT_EQ(online.session.State(), SessionState::Idle);
    EXPECT_TRUE(online.socket.connects.empty());
    scene.Leave();
    EXPECT_EQ(tested.store.Size(), 0u);
}

// Las 18 del login y las 3 de las ventanas, en ese orden.
TEST(LoginSceneTest, LeaveReleasesItsTwentyOneSlots)
{
    Harness tested;
    LoginScene& scene = tested.Make();
    ASSERT_TRUE(scene.Enter().HasValue());

    scene.Leave();

    std::vector<std::uint16_t> slots;
    for (const auto& entry : LoginTextureSet)
    {
        slots.push_back(entry.slot);
    }
    for (const auto& entry : WindowTextureSet)
    {
        slots.push_back(entry.slot);
    }
    EXPECT_EQ(tested.store.released, slots);
    EXPECT_EQ(tested.store.Size(), 0u);
}

// Sin Text.bmd en GameData (un test, o una tabla que no se leyó), lo demás se dibuja igual.
TEST(LoginSceneTest, WithoutTextsItDrawsTheRest)
{
    Harness tested;
    tested.data = GameData{};
    ASSERT_TRUE(tested.Make().Enter().HasValue());

    const std::vector<FrameEvent> events = tested.Frame();

    EXPECT_EQ(KindsOf(events),
              (std::vector<Kind>{Kind::Quad, Kind::Quad, Kind::Flush, Kind::Models, Kind::Quad, Kind::Quad}));
    EXPECT_TRUE(tested.target.Texts().empty());
}

// Los tests arman escenas en la pila, y la de MSVC es de 1 MB: las tablas grandes (los 3.000 lugares de
// las partículas, los 1.000 de los sprites) van en el heap, y la escena queda chica.
TEST(LoginSceneTest, FitsOnTheStackOfATest)
{
    constexpr std::size_t Limit = 128 * 1024;
    EXPECT_LT(sizeof(Client::Scenes::LoginScene), Limit);
}
} // namespace
