#include "Scenes/LoginScene.h"

#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Network/PacketSerials.h"
#include "Network/SocketError.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/ServerList.h"
#include "Render/SpriteBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/TextureSlot.h"
#include "Render/SpriteVertex.h"
#include "Scenes/GameData.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/LoginWindow.h"
#include "UI/MenuBar.h"
#include "UI/MessageArea.h"
#include "UI/NoticeWindow.h"
#include "UI/ServerListWindow.h"
#include "UI/Widget.h"
#include "support/Bytes.h"

#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FakeTextureStore.h"
#include "support/GateFiles.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/JpegVectors.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/SkillFiles.h"
#include "support/ScenePrinters.h"
#include "support/SessionPackets.h"
#include "support/TemporaryFolder.h"
#include "support/TerrainFiles.h"
#include "support/TestGameServer.h"
#include "support/UiInput.h"
#include "Scenes/LoginModels.h"
#include "support/FixedRandom.h"
#include "support/LoginLogoFiles.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Scenes::LoginScene con la sesión y sus ventanas (CL5.5): la lista cuando la sesión espera
// la elección, la cuenta cuando espera la cuenta y la clave, y los avisos de 06 § Los avisos. Sobre
// una sesión de verdad y un FakeSocket que hace de los dos servidores, con un Data\ sintético, la
// tabla de texturas falsa y el RecordingFrameTarget. Un "paso" es lo de GameLoop: la sesión, el
// Step de la escena y ConsumeEdges.
//
// Los textos son inventados, y la cuenta y la clave son las sintéticas: el login se compara por
// igualdad, sin imprimirlo.
namespace
{
using Client::App::Credentials;
using Client::App::PixelSize;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionFailure;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::App::TickCountMs;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Network::Connection;
using Client::Network::IdentityObfuscation;
using Client::Network::PacketSerials;
using Client::Network::SocketError;
using Client::Protocol::ClientVersion;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::ServerEntry;
using Client::Protocol::Connection::WriteLoginRequest;
using Client::Render::BlendMode;
using Client::Render::TextureSlot;
using Client::Scenes::GameData;
using Client::Scenes::LoadGameData;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::TextureBufferSize;
using Client::Scenes::WindowTextureSet;
using Client::Tests::AddressReply;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::FrameEvent;
using Client::Tests::GameClientKeys;
using Client::Tests::GameHello;
using Client::Tests::GameServerReceived;
using Client::Tests::Hello;
using Client::Tests::LoginResult;
using Client::Tests::LogoutResult;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::ServerList;
using Client::Tests::ServerListWithLoads;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextTable;
using Client::Tests::WriteTextureSet;
using Client::UI::LayoutLogin;
using Client::UI::LayoutNotice;
using Client::UI::LayoutServerList;
using Client::UI::LoginLayout;
using Client::UI::Rect;
using Client::UI::ServerListLayout;

using Kind = FrameEvent::Kind;

constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// Los slots de las ventanas: el botón de grupo y el marco de la lista, los dos botones y el adorno
// de la barra, el fondo de la cuenta y la caja del aviso.
constexpr TextureSlot GroupButtonSlot{0x0c};
constexpr TextureSlot FrameSlot{0x0f};
constexpr TextureSlot MenuSlot{0x10};
constexpr TextureSlot CreditSlot{0x11};
constexpr TextureSlot OrnamentSlot{0x12};
constexpr TextureSlot LoginBackgroundSlot{0x13};
constexpr TextureSlot NoticeBoxSlot{0x727};
// Los botones de la ventana de Esc (op1_b_all).
constexpr TextureSlot EscapeButtonSlot{0x72f};

// Textos inventados en los índices que usa el login.
constexpr std::string_view ConnectFailedText = "no conecto";
constexpr std::string_view EmptyAccountText = "falta la cuenta";
constexpr std::string_view EmptyPasswordText = "falta la clave";
constexpr std::string_view VersionFirstText = "otra version";
constexpr std::string_view VersionSecondText = "segunda linea";
constexpr std::string_view WrongPasswordText = "clave mala";
constexpr std::string_view FullServerText = "lleno";
constexpr std::string_view GroupTwentyText = "Veinte";

std::map<std::size_t, std::string> SessionTexts()
{
    return {{402, std::string(ConnectFailedText)},
            {403, std::string(EmptyAccountText)},
            {404, std::string(EmptyPasswordText)},
            {405, std::string(VersionFirstText)},
            {406, std::string(VersionSecondText)},
            {407, std::string(WrongPasswordText)},
            {416, std::string(FullServerText)},
            {450, "Cuenta"},
            {451, "Clave"},
            {460, "sin pvp %s-%d"},
            {461, "servidor %s-%d"},
            {470, "conectando"},
            {471, "espere"},
            {472, "mirando la cuenta"},
            {473, "espere otra vez"},
            {481, "conectado a %s:%d"},
            {560, "a tope"},
            {561, "ocupado"},
            {562, "libre"},
            {1829, std::string(GroupTwentyText)}};
}

std::vector<std::byte> ServerListOf(std::initializer_list<ServerEntry> entries)
{
    const std::vector<ServerEntry> list(entries);
    return ServerListWithLoads(list);
}

int MiddleX(const Rect& rect)
{
    return rect.x + rect.width / 2;
}

int MiddleY(const Rect& rect)
{
    return rect.y + rect.height / 2;
}

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

struct Harness
{
    // Con `withTheCloth`, lo global de la tela en el contexto (el viento, el contador de pasos y el rand()).
    // Con `world`, el chat del mundo y un medidor de seis píxeles por byte.
    explicit Harness(bool withTheCloth = false, Client::Scenes::WorldState* world = nullptr)
        : log(sink, LogLevel::Debug), connection(socket, IdentityObfuscation::Shared(), clock, log, 1),
          session(connection, clock, log, GameClientKeys(), SessionSettings{ServerAddress{"10.0.0.1", 44405}},
                  eventSink)
    {
        WriteTextureSet(folder.Path(), LoginTextureSet);
        WriteTextureSet(folder.Path(), WindowTextureSet);
        WriteTextTable(folder.Path(), SessionTexts());
        Client::Tests::WriteSceneFile(folder.Path(), "Gate.bmd", Client::Tests::GateTableBytes({}));
        Client::Tests::WriteSceneFile(folder.Path(), "Local/Item.bmd", Client::Tests::EmptyItemTableBytes());
        Client::Tests::WriteEmptyItemSetFiles(folder.Path());
        Client::Tests::WriteEmptySkillFile(folder.Path());
        auto loaded = LoadGameData(folder.Path());
        EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
        if (loaded.HasValue())
        {
            data = std::move(loaded).Value();
        }
        scene.emplace(SceneContext{.dataRoot = folder.Path(),
                                   .textures = store,
                                   .log = log,
                                   .cloth = withTheCloth ? &cloth : nullptr,
                                   .world = world,
                                   .textMeasure = &letters},
                      data, models, &session, "0.0.0", FixedSeed);
    }

    // Lo de un frame del GameLoop con un paso: la sesión, el paso de la escena y los flancos.
    void Step()
    {
        static_cast<void>(session.Poll());
        scene->Step(input);
        input.ConsumeEdges();
    }

    // Un frame dibujado.
    std::vector<FrameEvent> Frame(Client::Core::Duration frameClock = std::chrono::milliseconds{40})
    {
        Client::Scenes::SceneCanvas& canvas = target.BeginFrame(frameClock);
        scene->Render(canvas, 0.0f);
        target.Present();
        return target.frames.back().events;
    }

    // Enter, el saludo del CS y la lista: la ventana de la lista está abierta.
    void ReachTheList(const std::vector<std::byte>& list)
    {
        const auto entered = scene->Enter();
        ASSERT_TRUE(entered.HasValue()) << entered.Error();
        socket.reads = {Hello()};
        Step();
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
        socket.reads = {list};
        Step();
        ASSERT_EQ(session.State(), SessionState::ChoosingServer);
    }

    // La ubicación de la lista de `count` servidores, con la caja de la descripción del buffer de
    // server_ex02 (slot 0x19) que subió la escena.
    [[nodiscard]] ServerListLayout ListLayout(std::size_t count) const
    {
        const auto line = store.Find(TextureSlot{0x19});
        EXPECT_TRUE(line.has_value());
        const TextureBufferSize buffer =
            line ? TextureBufferSize{line->texture.textureWidth, line->texture.textureHeight} : TextureBufferSize{};
        return LayoutServerList(PixelSize{640, 480}, Client::UI::DescriptionHeight(buffer), count);
    }

    // Un clic en el centro de `rect`, en un paso.
    void ClickOn(const Rect& rect)
    {
        Client::Tests::ClickAt(input, MiddleX(rect), MiddleY(rect));
        Step();
    }

    void Tap(Key key)
    {
        Client::Tests::TapKey(input, key);
        Step();
    }

    void Type(std::string_view text)
    {
        Client::Tests::TypeText(input, std::string(text));
        Step();
    }

    // La lista del laboratorio (el 400, del grupo 20), su grupo y su servidor: la sesión espera la
    // dirección.
    void ChooseTheLabServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachTheList(ServerList({400})));
        const ServerListLayout layout = ListLayout(1);
        ClickOn(layout.groupButtons[0]);
        ClickOn(layout.serverButtons[0]);
        ASSERT_EQ(session.State(), SessionState::WaitingServerAddress);
    }

    // Y la dirección, el Opened del GS y su saludo: la ventana de la cuenta está abierta.
    void ReachTheLogin()
    {
        ASSERT_NO_FATAL_FAILURE(ChooseTheLabServer());
        socket.reads = {AddressReply(GameHost, GamePort)};
        Step();
        gameServerStart = socket.sent.size();
        Step();
        ASSERT_EQ(session.State(), SessionState::WaitingGameHello);
        socket.reads = {GameHello(1, ClientVersion)};
        Step();
        ASSERT_EQ(session.State(), SessionState::WaitingCredentials);
    }

    // La cuenta, Tab, la clave y Enter, un paso cada uno.
    void TypeTheCredentials()
    {
        Type(Account);
        Tap(Key::Tab);
        Type(Password);
        Tap(Key::Enter);
    }

    // Los quads de `slot` en `events`.
    [[nodiscard]] static std::size_t QuadsOf(const std::vector<FrameEvent>& events, TextureSlot slot)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            events, [slot](const FrameEvent& event) { return event.kind == Kind::Quad && event.texture == slot; }));
    }

    // Las texturas de los quads de `events` que son de `slots`, en orden.
    [[nodiscard]] static std::vector<TextureSlot> QuadsAmong(const std::vector<FrameEvent>& events,
                                                             const std::vector<TextureSlot>& slots)
    {
        std::vector<TextureSlot> found;
        for (const FrameEvent& event : events)
        {
            if (event.kind == Kind::Quad && event.texture && std::ranges::find(slots, *event.texture) != slots.end())
            {
                found.push_back(*event.texture);
            }
        }
        return found;
    }

    // La v de arriba del primer quad de `slot`, o nada.
    [[nodiscard]] static std::optional<float> TopV(const std::vector<FrameEvent>& events, TextureSlot slot)
    {
        const auto quad = std::ranges::find_if(events, [slot](const FrameEvent& event)
                                               { return event.kind == Kind::Quad && event.texture == slot; });
        if (quad == events.end())
        {
            return std::nullopt;
        }
        return quad->vertices[0].v;
    }

    // El alto del buffer que subió la escena para `slot`.
    [[nodiscard]] float BufferHeight(TextureSlot slot) const
    {
        const auto found = store.Find(slot);
        EXPECT_TRUE(found.has_value());
        return found ? static_cast<float>(found->texture.textureHeight) : 1.0f;
    }

    // Un paso con el mouse en el medio de `rect`, sin apretar.
    void HoverOn(const Rect& rect)
    {
        Client::Tests::MoveMouse(input, MiddleX(rect), MiddleY(rect));
        Step();
    }

    // Si algún texto de `events` es `text`.
    [[nodiscard]] static bool Drew(const std::vector<FrameEvent>& events, std::string_view text)
    {
        return std::ranges::any_of(events, [text](const FrameEvent& event)
                                   { return event.kind == Kind::Text && event.text.text == text; });
    }

    [[nodiscard]] GameServerReceived GameServerReads() const
    {
        PacketSerials serials;
        return Client::Tests::AsTheGameServerReceives(std::span(socket.sent).subspan(gameServerStart), serials);
    }

    [[nodiscard]] bool Logged(LogLevel level, std::string_view part) const
    {
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool LoggedTheCredentials() const
    {
        return std::ranges::any_of(
            sink.lines, [](const std::string& line)
            { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; });
    }

    TemporaryFolder folder{"login-scene-session"};
    LetterMeasure letters;
    FakeTextureStore store;
    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log;
    Connection connection;
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session;
    GameData data;
    Client::Scenes::SingleModelLibrary models;
    RecordingFrameTarget target;
    InputState input;
    Client::Render::Models::ClothWorld cloth;
    std::optional<LoginScene> scene;
    std::size_t gameServerStart = 0;
};

// Hasta que llega la lista no hay ventana; con la lista, la escena abre la suya (0x4c1860 la abre
// con el F4 06), con un botón por grupo y el nombre del grupo, después de los textos del login y
// con la mezcla normal.
TEST(LoginSceneSessionTest, TheListShowsWhenItArrives)
{
    Harness tested;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.socket.reads = {Hello()};
    tested.Step();
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), GroupButtonSlot), 0u);

    tested.socket.reads = {ServerList({400})};
    tested.Step();
    const std::vector<FrameEvent> events = tested.Frame();

    ASSERT_EQ(Harness::QuadsOf(events, GroupButtonSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, GroupTwentyText));
    const auto firstWindowQuad = std::ranges::find_if(
        events, [](const FrameEvent& event) { return event.kind == Kind::Quad && event.texture == FrameSlot; });
    ASSERT_NE(firstWindowQuad, events.end());
    // Los fondos, el fin del 2D, los modelos (con el terreno), las dos arenas y los tres textos, y
    // después la UI: sin grupo elegido, la pieza del marco de la izquierda y el botón.
    EXPECT_EQ(firstWindowQuad - events.begin(), 9);
    EXPECT_EQ(firstWindowQuad->blend, BlendMode::Alpha);
    const auto button = std::ranges::find_if(std::next(firstWindowQuad), events.end(),
                                             [](const FrameEvent& event) { return event.kind == Kind::Quad; });
    ASSERT_NE(button, events.end());
    EXPECT_EQ(button->texture, GroupButtonSlot);
}

// 0x4c1860 abre la barra de abajo después de la lista, así que va arriba de ella. Nada la cierra en
// el login: sigue con la cuenta, debajo de su ventana, cuando se cancela y cuando vuelve la lista,
// que la abre otra vez sin repetirla.
TEST(LoginSceneSessionTest, TheBarOpensWithTheListAndStays)
{
    Harness tested;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.socket.reads = {Hello()};
    tested.Step();
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), OrnamentSlot), 0u);
    tested.socket.reads = {ServerList({400})};
    tested.Step();
    const std::vector<TextureSlot> order{GroupButtonSlot, OrnamentSlot, MenuSlot, CreditSlot, LoginBackgroundSlot};
    EXPECT_EQ(Harness::QuadsAmong(tested.Frame(), order),
              (std::vector<TextureSlot>{GroupButtonSlot, OrnamentSlot, MenuSlot, CreditSlot}));

    Harness login;
    ASSERT_NO_FATAL_FAILURE(login.ReachTheLogin());
    EXPECT_EQ(Harness::QuadsAmong(login.Frame(), order),
              (std::vector<TextureSlot>{OrnamentSlot, MenuSlot, CreditSlot, LoginBackgroundSlot}));
    login.Tap(Key::Escape);
    EXPECT_EQ(Harness::QuadsAmong(login.Frame(), order),
              (std::vector<TextureSlot>{OrnamentSlot, MenuSlot, CreditSlot}));
    login.socket.reads = {Hello()};
    login.Step();
    login.socket.reads = {ServerList({400})};
    login.Step();
    ASSERT_EQ(login.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(Harness::QuadsAmong(login.Frame(), order),
              (std::vector<TextureSlot>{GroupButtonSlot, OrnamentSlot, MenuSlot, CreditSlot}));
}

// La barra y la lista, por el administrador (Q-C84): la barra se abrió última, así que está arriba con el
// foco, y con el mouse encima los créditos muestran el cuadro 1. Su clic se corta (P-C76) sin mandar nada.
// El del menú abre la de Esc (0x6411d8), de toda la pantalla, y Esc la cierra: el apretón lo toma ella y el
// paso no la vuelve a abrir (Q-C45). Apretar en la lista la pasa arriba y atiende su clic. Con un aviso
// abierto, el aviso tiene el mouse, como en el original, donde es de toda la pantalla.
TEST(LoginSceneSessionTest, TheBarStepsWithTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerListOf({{400, 100}})));
    const Client::UI::MenuBarLayout bar = Client::UI::LayoutMenuBar(PixelSize{640, 480});
    const float height = tested.BufferHeight(CreditSlot);

    tested.HoverOn(bar.credit);
    EXPECT_EQ(Harness::TopV(tested.Frame(), CreditSlot), (30.0f + 0.5f) / height);

    const std::size_t sent = tested.socket.sent.size();
    tested.ClickOn(bar.credit);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "los créditos (+0x4578, 0x63f020)"));
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), EscapeButtonSlot), 0u);
    tested.ClickOn(bar.menu);
    // En el login, el de otro servidor no se dibuja: tres botones.
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), EscapeButtonSlot), 3u);
    tested.Tap(Key::Escape);
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), EscapeButtonSlot), 0u);
    EXPECT_EQ(tested.socket.sent.size(), sent);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);

    // El 400 con carga 100: el aviso 0x63.
    const ServerListLayout layout = tested.ListLayout(1);
    tested.ClickOn(layout.groupButtons[0]);
    tested.ClickOn(layout.serverButtons[0]);
    ASSERT_EQ(Harness::QuadsOf(tested.Frame(), NoticeBoxSlot), 1u);
    tested.HoverOn(bar.credit);
    EXPECT_EQ(Harness::TopV(tested.Frame(), CreditSlot), 0.5f / height);
}

// Y con la cuenta (Q-C84): la cuenta está arriba, pero el mouse lo tiene la primera desde arriba que lo
// tiene adentro, y los créditos muestran el cuadro 1. El menú abre la de Esc arriba de la cuenta, sin
// mandar nada ni cerrarla ni sacarle el teclado. Con la cuenta abierta, Esc cierra la de Esc y el paso no la
// vuelve a abrir (0x634d59).
TEST(LoginSceneSessionTest, TheBarStepsWithTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    const Client::UI::MenuBarLayout bar = Client::UI::LayoutMenuBar(PixelSize{640, 480});
    const float height = tested.BufferHeight(CreditSlot);

    tested.HoverOn(bar.credit);
    EXPECT_EQ(Harness::TopV(tested.Frame(), CreditSlot), (30.0f + 0.5f) / height);

    const std::size_t sent = tested.socket.sent.size();
    tested.ClickOn(bar.menu);
    EXPECT_EQ(tested.socket.sent.size(), sent);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.scene->WantsTextInput());
    const std::vector<FrameEvent> frame = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(frame, LoginBackgroundSlot), 1u);
    EXPECT_EQ(Harness::QuadsOf(frame, EscapeButtonSlot), 3u);
    tested.Tap(Key::Escape);
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), EscapeButtonSlot), 0u);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
}

// El grupo y después el servidor: la escena le pasa el código a la sesión, que manda el F4 03 (el
// 400, 90 01), y la lista se cierra.
TEST(LoginSceneSessionTest, ClickingAServerChoosesIt)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400})));
    const ServerListLayout layout = tested.ListLayout(1);

    tested.ClickOn(layout.groupButtons[0]);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    tested.ClickOn(layout.serverButtons[0]);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{std::uint16_t{400}});
    const std::vector<std::byte> choice = Client::Tests::Bytes({0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01});
    ASSERT_GE(tested.socket.sent.size(), choice.size());
    EXPECT_TRUE(Client::Tests::SameBytes(std::span(tested.socket.sent).last(choice.size()), choice));
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), GroupButtonSlot), 0u);
}

// Un servidor con carga de 100 a 127: el aviso 0x63 con su texto, sin F4 03, y la lista queda
// abajo; con el OK se cierra el aviso y sigue la lista.
TEST(LoginSceneSessionTest, AFullServerShowsItsNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerListOf({{400, 100}})));
    const ServerListLayout layout = tested.ListLayout(1);
    const std::size_t sent = tested.socket.sent.size();

    tested.ClickOn(layout.groupButtons[0]);
    tested.ClickOn(layout.serverButtons[0]);

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(tested.socket.sent.size(), sent);
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, FullServerText));
    EXPECT_EQ(Harness::QuadsOf(events, GroupButtonSlot), 1u);

    tested.Tap(Key::Enter);
    events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, GroupButtonSlot), 1u);
    EXPECT_FALSE(tested.scene->WantsToQuit());
}

// Un servidor sin botón (el 420 es del grupo 21) no se muestra, y el log lo dice con la cuenta.
TEST(LoginSceneSessionTest, TheServersThatAreNotShownAreLogged)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400, 420})));

    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "1 servidores de la lista no se muestran"));
}

// 0x4c1e20: con el saludo del GS se abre la cuenta, con el título del servidor elegido (el 461:
// el grupo 20 va en el lugar 4, sin la marca de sin PvP) y el foco en la cuenta.
TEST(LoginSceneSessionTest, TheLoginWindowShowsWithTheGameServer)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.Step();
    tested.Step();
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), LoginBackgroundSlot), 0u);

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.Step();
    const std::vector<FrameEvent> events = tested.Frame();

    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, "servidor Veinte-1"));
}

// El área de mensajes del login: el clic en un servidor deja el 470 y el 471 (0x64630f y 0x646320), la respuesta al F4
// 03 el 481 con la dirección y el puerto en sus marcas (0x4e33d0) y el envío de la cuenta el 472 y el 473 (0x6427d9 y
// 0x6427ea), todos como sistema y sin nombre.
TEST(LoginSceneSessionTest, TheLoginWritesItsStepsInTheMessageArea)
{
    Client::Scenes::WorldState world;
    Harness tested(false, &world);
    // El mundo 56 del login con un terreno plano, para que su personaje tenga dónde pararse (0x511b9f).
    const std::filesystem::path& root = tested.folder.Path();
    Client::Tests::WriteSceneFile(root, "World56/TerrainHeight.OZB",
                                  std::vector<std::byte>(Client::Data::Terrain::TerrainHeightsSize));
    Client::Tests::WriteSceneFile(root, "World56/TerrainLight.OZJ",
                                  Client::Tests::OzjOf(Client::Tests::ReadJpegVector("light-256.jpg")));
    Client::Tests::WriteSceneFile(root, "World56/EncTerrain56.att",
                                  Client::Tests::EncryptTerrainAttributes(
                                      Client::Tests::PlainTerrainAttributes(56, Client::Tests::AttributeCells::Bytes)));
    Client::Tests::WriteSceneFile(root, "World56/EncTerrain56.map",
                                  Client::Tests::EncryptK16(Client::Tests::PlainTerrainMapping(56)));
    for (const char* texture : {"Interface/accept_box01.OZJ", "World56/TileGrass01.OZJ", "Object8/drop01.OZJ"})
        Client::Tests::WriteSceneFile(root, texture,
                                      Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    const Client::UI::MessageAreaState& area = world.Runtime().chat.area;
    const auto row = [&area](std::int32_t back) -> const Client::UI::MessageAreaRow&
    { return area.rows[static_cast<std::size_t>(area.count - back)]; };

    tested.models.Set(Client::Data::Models::PlayerSkeletonModel, Client::Data::Models::Model{},
                      Client::Tests::PlayerActionsSkeleton(), {});
    const auto entered = tested.scene->Enter();
    ASSERT_TRUE(entered.HasValue()) << entered.Error();
    // Sin el cuervo del personaje del login: esta biblioteca no lo tiene.
    for (auto& actor : world.Actors())
        actor.darkSpirit.reset();
    tested.socket.reads = {Hello()};
    tested.Step();
    tested.socket.reads = {ServerList({400})};
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    const ServerListLayout layout = tested.ListLayout(1);
    tested.ClickOn(layout.groupButtons[0]);
    tested.ClickOn(layout.serverButtons[0]);
    ASSERT_FALSE(tested.scene->Failure().has_value()) << *tested.scene->Failure();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(row(2).text, "conectando");
    EXPECT_EQ(row(1).text, "espere");
    EXPECT_EQ(row(1).name, "");
    EXPECT_EQ(row(1).type, Client::UI::SystemMessage);

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.Step();
    EXPECT_EQ(row(1).text, "conectado a 10.0.0.2:55901");
    tested.Step();
    EXPECT_EQ(row(1).text, "conectado a 10.0.0.2:55901") << "una vez por dirección";
    EXPECT_EQ(row(2).text, "espere");

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    tested.TypeTheCredentials();
    EXPECT_EQ(row(2).text, "mirando la cuenta");
    EXPECT_EQ(row(1).text, "espere otra vez");
}

// Lo tecleado va a la cuenta, Tab pasa a la clave y Enter manda: la sesión manda el login con la
// cuenta y la clave tecleadas, sin que ninguna llegue al log.
TEST(LoginSceneSessionTest, TypingAndEnterSubmitsTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    tested.TypeTheCredentials();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 1u);
    const std::span<const std::byte> login = received.packets[0].Bytes();
    const std::span<const std::byte> written = expected.Value().packet.Bytes();
    EXPECT_TRUE(Client::Tests::SameBytesWithoutValues(login, written));
    // Por igualdad, para no imprimir el login si falla.
    EXPECT_TRUE(std::ranges::equal(login, written));
    EXPECT_FALSE(tested.LoggedTheCredentials());
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), LoginBackgroundSlot), 0u);
}

// Enter con la cuenta vacía: el aviso 0x64 con su texto, y nada sale. El Enter que lo cierra no le
// llega a la cuenta, que se abre en ese paso con el foco en la cuenta (0x643560): con el segundo
// Enter, el aviso se cierra y la cuenta queda abierta, sin otro aviso.
TEST(LoginSceneSessionTest, AnEmptyAccountShowsItsNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    tested.Tap(Key::Enter);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, EmptyAccountText));
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 0u);

    for (int interval = 0; interval < 3; ++interval)
    {
        tested.clock.Advance(std::chrono::seconds{20});
        tested.Step();
    }
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(Harness::Drew(tested.Frame(), EmptyAccountText));
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);

    tested.Tap(Key::Enter);

    events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 1u);
    tested.TypeTheCredentials();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
}

// Con la cuenta y sin la clave: el aviso 0x65 con el texto 404, y su OK abre la cuenta con el foco
// en la clave (0x643560), así que lo que se teclea después va a la clave.
TEST(LoginSceneSessionTest, AnEmptyPasswordShowsItsNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.Type(Account);

    tested.Tap(Key::Enter);

    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, EmptyPasswordText));

    for (int interval = 0; interval < 3; ++interval)
    {
        tested.clock.Advance(std::chrono::seconds{20});
        tested.Step();
    }
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(Harness::Drew(tested.Frame(), EmptyPasswordText));
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);

    tested.Tap(Key::Enter);
    tested.Type(Password);
    tested.Tap(Key::Enter);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
}

// Un login rechazado con el 0: el aviso 0x15 (el texto 407); su OK abre otra vez la cuenta con el
// foco en la clave, que quedó vacía, y la cuenta tecleada sigue (0x643560).
TEST(LoginSceneSessionTest, ARejectedLoginShowsItsNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.TypeTheCredentials();

    tested.socket.reads = {LoginResult(0)};
    tested.Step();

    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, WrongPasswordText));

    for (int interval = 0; interval < 3; ++interval)
    {
        tested.clock.Advance(std::chrono::seconds{20});
        tested.Step();
    }
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(Harness::Drew(tested.Frame(), WrongPasswordText));
    EXPECT_EQ(tested.GameServerReads().packets.size(), 4u);

    tested.Tap(Key::Enter);
    events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, Account));
    EXPECT_FALSE(Harness::Drew(events, "********"));

    tested.Type(Password);
    tested.Tap(Key::Enter);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 5u);
}

// El texto tecleado, solo con la cuenta arriba: ni con la lista, ni con un aviso que la tapa, ni
// después de mandar. Main prende y apaga el de SDL con esto (CL5.5).
TEST(LoginSceneSessionTest, TheSceneWantsTheTextOnlyWithTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400})));
    EXPECT_FALSE(tested.scene->WantsTextInput());
    const ServerListLayout layout = tested.ListLayout(1);
    tested.ClickOn(layout.groupButtons[0]);
    tested.ClickOn(layout.serverButtons[0]);
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_FALSE(tested.scene->WantsTextInput());
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.Step();
    tested.Step();
    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.scene->WantsTextInput());

    tested.Tap(Key::Enter);
    EXPECT_FALSE(tested.scene->WantsTextInput());
    tested.Tap(Key::Enter);
    EXPECT_TRUE(tested.scene->WantsTextInput());

    tested.TypeTheCredentials();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_FALSE(tested.scene->WantsTextInput());
}

// Leave cierra las ventanas, como suelta sus texturas: la cuenta, con la clave tecleada, no queda
// abierta ni pide el texto.
TEST(LoginSceneSessionTest, LeavingClosesTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.Tap(Key::Tab);
    tested.Type(Password);

    tested.scene->Leave();

    EXPECT_FALSE(tested.scene->WantsTextInput());
}

// Entrar otra vez empieza de cero: sin la lista, la barra ni el aviso de antes, que no se dibujan
// hasta que la sesión los pida otra vez.
TEST(LoginSceneSessionTest, EnteringAgainClosesTheWindows)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerListOf({{400, 100}})));
    const ServerListLayout layout = tested.ListLayout(1);
    tested.ClickOn(layout.groupButtons[0]);
    tested.ClickOn(layout.serverButtons[0]);
    std::vector<FrameEvent> events = tested.Frame();
    ASSERT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    ASSERT_EQ(Harness::QuadsOf(events, GroupButtonSlot), 1u);

    ASSERT_TRUE(tested.scene->Enter().HasValue());

    events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, GroupButtonSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, OrnamentSlot), 0u);
}

// Y con la cuenta abierta: entrar otra vez la cierra.
TEST(LoginSceneSessionTest, EnteringAgainClosesTheLogin)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    ASSERT_TRUE(tested.scene->Enter().HasValue());

    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), LoginBackgroundSlot), 0u);
    EXPECT_FALSE(tested.scene->WantsTextInput());
}

// Un ConnectServer que rechaza la conexión: el aviso 0x70, con el texto 402 (0x4c1340).
TEST(LoginSceneSessionTest, ARefusedConnectShowsText402)
{
    Harness tested;
    tested.socket.connectResult = SocketError::Refused;
    ASSERT_TRUE(tested.scene->Enter().HasValue());

    tested.Step();

    ASSERT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, ConnectFailedText));
}

// El connect al CS del original es bloqueante y corre antes del primer frame del login (0x6349eb
// llama a 0x4c1340, que llama a connect en 0x4e8625): en la pantalla queda la carga hasta que
// termina, y el primer frame del login ya trae el aviso (Q-C85). Acá el connect no bloquea, y la
// escena retiene el último frame mientras conecta: el GameLoop no dibuja. Lo suelta cuando la
// escena vio la falla, con el aviso abierto, no cuando la sesión falla: un frame dibujado entre el
// Poll y el Step sería el login sin el aviso.
TEST(LoginSceneSessionTest, AFailingConnectHoldsTheFrameUntilTheNotice)
{
    Harness tested;
    tested.socket.pollsUntilConnected = 2;
    tested.socket.connectResult = SocketError::Refused;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.Step();
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.scene->Step(tested.input);
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, ConnectFailedText));
}

// Un connect que falla enseguida (un puerto cerrado en la misma máquina, un host que no resuelve)
// sale en el primer Poll después de Enter, y en el modo normal ese frame puede no tener un paso
// antes del dibujo. La escena todavía no vio la falla: retiene el frame hasta que su paso abre el
// aviso, y el primer frame del login ya lo trae.
TEST(LoginSceneSessionTest, AConnectThatFailsAtOnceHoldsTheFrameUntilTheNotice)
{
    Harness tested;
    tested.socket.connectResult = SocketError::Refused;
    ASSERT_TRUE(tested.scene->Enter().HasValue());

    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.scene->Step(tested.input);
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
    EXPECT_TRUE(Harness::Drew(tested.Frame(), ConnectFailedText));
}

// Lo mismo con el GS: el F4 03 lo abre en un Poll y la falla sale en el siguiente, sin un paso en
// el medio (dos frames sin pasos). La escena no llegó a verla conectando, y retiene el frame igual.
TEST(LoginSceneSessionTest, AGameServerConnectThatFailsBetweenStepsHoldsTheFrame)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    tested.socket.connectResult = SocketError::Refused;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};

    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.scene->Step(tested.input);
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
    EXPECT_TRUE(Harness::Drew(tested.Frame(), ConnectFailedText));
}

// Con el CS abierto el original sigue con el login y dibuja mientras espera el saludo: la escena
// suelta el frame.
TEST(LoginSceneSessionTest, AnOpenedConnectionDrawsTheLogin)
{
    Harness tested;
    tested.socket.pollsUntilConnected = 1;
    ASSERT_TRUE(tested.scene->Enter().HasValue());

    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
}

// El connect al GS también bloquea en el original (0x4e3396): con la dirección, en la pantalla
// queda el último frame, ya sin la lista (la cerró el clic, 0x645e10), hasta que el GS abre.
TEST(LoginSceneSessionTest, TheGameServerConnectHoldsTheFrame)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.socket.pollsUntilConnected = 1;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};

    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.scene->HoldsTheLastFrame());

    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_FALSE(tested.scene->HoldsTheLastFrame());
}

// Sin modelos del personaje ni .obj, solo el dueño del dragón y el logo consumen RNG al dibujar:
// dos rand comunes por frame original. El tiempo conectado no debe producir 64 frames de catch-up.
TEST(LoginSceneSessionTest, ObjectEffectsResumeWithOneFrameAfterAHeldFrame)
{
    Harness tested(true);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.clock.Advance(std::chrono::milliseconds{40});
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    ASSERT_EQ(tested.cloth.random.State(), 0xc6192e9eu) << "un frame inicial de dos objetos";
    tested.socket.pollsUntilConnected = 2;
    tested.session.Start();
    tested.scene->Step(tested.input);
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    ASSERT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.scene->OnFrameHeld();
    tested.clock.Advance(std::chrono::seconds{5});
    tested.Step();
    tested.Step();
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    EXPECT_EQ(tested.cloth.random.State(), 0xc6192e9eu) << "solo un frame al volver a dibujar";
}

TEST(LoginSceneSessionTest, AConnectStartedByTheUiCannotAccumulateObjectFramesBetweenSteps)
{
    Harness tested(true);
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.clock.Advance(std::chrono::milliseconds{40});
    (void)tested.Frame(tested.clock.Now());
    tested.socket.pollsUntilConnected = 0;
    tested.Tap(Key::Escape);
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    ASSERT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.scene->OnFrameHeld();
    tested.clock.Advance(std::chrono::seconds{5});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    // El connect ya terminó cuando llega el próximo Step; el evento ya descartó el tiempo retenido.
    tested.scene->Step(tested.input);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    EXPECT_EQ(tested.cloth.random.State(), 0xc6192e9eu) << "dos objetos, ningún backlog del connect";
}

TEST(LoginSceneSessionTest, AHeldFrameNotifiesObjectEffectsWhenTheWholeConnectRunsBetweenSteps)
{
    Harness tested(true);
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    tested.clock.Advance(std::chrono::milliseconds{40});
    (void)tested.Frame(tested.clock.Now());
    tested.socket.connectResult = SocketError::Refused;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    ASSERT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.scene->OnFrameHeld();
    tested.scene->OnFrameHeld(); // El aviso repetido conserva el mismo reset, sin RNG ni efectos nuevos.
    tested.clock.Advance(std::chrono::seconds{5});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::Failed);
    tested.scene->Step(tested.input);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    EXPECT_EQ(tested.cloth.random.State(), 0xc6192e9eu) << "un frame para dueño del dragón y logo";
}

// Retener el frame hasta que Step abra la lista no representa un connect bloqueante:
// conserva los 20 ms de fase y completa una sola ronda al llegar a 47 ms desde el último dibujo.
TEST(LoginSceneSessionTest, HoldingForANormalSessionChangePreservesTheObjectFramePhase)
{
    Harness tested(true);
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.socket.reads = {Hello()};
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerList);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.clock.Advance(std::chrono::milliseconds{40});
    (void)tested.Frame(tested.clock.Now());
    tested.socket.reads = {ServerList({400})};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    ASSERT_TRUE(tested.scene->HoldsTheLastFrame());
    tested.scene->OnFrameHeld();
    tested.clock.Advance(std::chrono::milliseconds{20});
    tested.scene->Step(tested.input);
    ASSERT_FALSE(tested.scene->HoldsTheLastFrame());
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    EXPECT_EQ(tested.cloth.random.State(), 4u) << "20 ms no alcanzan para otra ronda";
    tested.clock.Advance(std::chrono::milliseconds{27});
    tested.cloth.random = Client::Render::Models::CrtRandom(4);
    (void)tested.Frame(tested.clock.Now());
    EXPECT_EQ(tested.cloth.random.State(), 0xc6192e9eu) << "la fase de 20 ms se conserva y completa 47 ms";
}

// El otro lado que cierra (el FD_CLOSE de 03): con la cuenta abierta, la cuenta se cierra y sale el
// 0x70. El texto 3 que el original pone antes en el área de mensajes (0x555190) es del chat, que
// llega con la CL8.
TEST(LoginSceneSessionTest, APeerCloseShowsTheConnectNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    tested.socket.peerClosed = true;
    tested.Step();

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 0u);
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, ConnectFailedText));
}

// La orden del GS de cerrar el juego (F1 02 con 0, Q-C106), con la cuenta abierta y el cierre del
// GS detrás: no es un GameServerLost, así que no sale el 0x70 y la cuenta queda como estaba, con
// su título. El original no corre otro paso después del WM_DESTROY: el Esc de ese mismo paso no
// cancela la cuenta ni vuelve a conectar. El juego lo cierra main.cpp, con la sesión, y no la
// escena.
TEST(LoginSceneSessionTest, AQuitOrderLeavesTheLoginAsItWas)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    tested.socket.reads = {LogoutResult(0)};
    tested.socket.peerClosed = true;
    tested.Tap(Key::Escape);

    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 1u);
    EXPECT_TRUE(Harness::Drew(events, "servidor Veinte-1"));
    EXPECT_EQ(Harness::QuadsOf(events, NoticeBoxSlot), 0u);
    EXPECT_FALSE(Harness::Drew(events, ConnectFailedText));
    EXPECT_FALSE(tested.scene->WantsToQuit());
    EXPECT_FALSE(tested.Logged(LogLevel::Info, "se cancela la cuenta"));
}

// El color de la primera malla del logo (la última en el lote, después del personaje), o ninguno.
std::optional<Client::Render::Color> LogoColorOf(const std::vector<FrameEvent>& events)
{
    const auto models = std::ranges::find(events, Kind::Models, &FrameEvent::kind);
    if (models == events.end() || models->modelVertices.size() < 6)
    {
        return std::nullopt;
    }
    return models->modelVertices[models->modelVertices.size() - 6].color;
}

// Q-C106: tampoco el mundo del login avanza en el paso de la orden de cerrar: la luz del logo queda
// como en el paso de antes, que sí la cambió.
TEST(LoginSceneSessionTest, AQuitOrderDoesNotMoveTheLoginWorld)
{
    Harness tested;
    Client::Tests::WriteLoginLogo(tested.folder.Path());
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    const std::optional<Client::Render::Color> before = LogoColorOf(tested.Frame());
    tested.Step();
    const std::optional<Client::Render::Color> lit = LogoColorOf(tested.Frame());
    ASSERT_TRUE(before.has_value());
    ASSERT_TRUE(lit.has_value());
    ASSERT_NE(lit->red, before->red);

    tested.socket.reads = {LogoutResult(0)};
    tested.socket.peerClosed = true;
    tested.Tap(Key::Escape);

    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::optional<Client::Render::Color> after = LogoColorOf(tested.Frame());
    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->red, lit->red);
}

// El connect del original bloquea dentro de un paso (Q-C85), y lo que tarda no entra al acumulador,
// que mide desde después de los pasos (02): mientras conecta no corre ningún paso, y el mundo del login
// no avanza. Con el CS de la entrada: la luz del logo, que ya se dibujó, queda como al entrar por más
// pasos de MuRet que pasen conectando, y sigue cuando el CS abre.
TEST(LoginSceneSessionTest, TheLoginWorldWaitsForTheConnectServer)
{
    Harness tested(true);
    Client::Tests::WriteLoginLogo(tested.folder.Path());
    tested.socket.pollsUntilConnected = 3;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    const std::optional<Client::Render::Color> entered = LogoColorOf(tested.Frame());
    ASSERT_TRUE(entered.has_value());
    const std::int32_t clothSteps = tested.cloth.logicSteps;
    // Lo que sacó el último dibujo: el del test, porque en el bucle el frame queda retenido.
    std::uint32_t random = tested.cloth.random.State();

    int connecting = 0;
    for (tested.Step(); tested.session.State() == SessionState::ConnectingConnectServer && connecting < 10;
         tested.Step())
    {
        ++connecting;
        // Ni la tela: el paso no la corre ni saca un rand() de la CRT.
        EXPECT_EQ(tested.cloth.logicSteps, clothSteps) << connecting;
        EXPECT_EQ(tested.cloth.random.State(), random) << connecting;
        const std::optional<Client::Render::Color> waiting = LogoColorOf(tested.Frame());
        ASSERT_TRUE(waiting.has_value());
        EXPECT_FLOAT_EQ(waiting->red, entered->red) << connecting;
        random = tested.cloth.random.State();
    }
    ASSERT_GE(connecting, 2);
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_EQ(tested.cloth.logicSteps, clothSteps + 1) << "el paso en que abrió ya corre";
    const std::optional<Client::Render::Color> opened = LogoColorOf(tested.Frame());
    ASSERT_TRUE(opened.has_value());
    EXPECT_NE(opened->red, entered->red);
}

// Lo mismo con el GS (0x4e3396): con la dirección, la luz queda como en el último paso antes de ella.
TEST(LoginSceneSessionTest, TheLoginWorldWaitsForTheGameServer)
{
    Harness tested(true);
    Client::Tests::WriteLoginLogo(tested.folder.Path());
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    (void)tested.Frame();
    tested.Step();
    const std::optional<Client::Render::Color> before = LogoColorOf(tested.Frame());
    ASSERT_TRUE(before.has_value());
    tested.socket.pollsUntilConnected = 3;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    // El paso que recibe la dirección todavía corre la tela: el connect empieza en él.
    tested.Step();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    const std::int32_t clothSteps = tested.cloth.logicSteps;

    int connecting = 0;
    for (std::uint32_t random = tested.cloth.random.State();
         tested.session.State() == SessionState::ConnectingGameServer && connecting < 10; tested.Step())
    {
        ++connecting;
        EXPECT_EQ(tested.cloth.logicSteps, clothSteps) << connecting;
        EXPECT_EQ(tested.cloth.random.State(), random) << connecting;
        const std::optional<Client::Render::Color> waiting = LogoColorOf(tested.Frame());
        ASSERT_TRUE(waiting.has_value());
        EXPECT_FLOAT_EQ(waiting->red, before->red) << connecting;
        random = tested.cloth.random.State();
    }
    ASSERT_GE(connecting, 2);
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.cloth.logicSteps, clothSteps + 1) << "el paso en que abrió ya corre";
    const std::optional<Client::Render::Color> opened = LogoColorOf(tested.Frame());
    ASSERT_TRUE(opened.has_value());
    EXPECT_NE(opened->red, before->red);
}

// El OK del 0x70 cierra el juego (Q-C69): la escena lo pide con WantsToQuit, con una línea en el
// log.
TEST(LoginSceneSessionTest, OkOnTheConnectNoticeQuits)
{
    Harness tested;
    tested.socket.connectResult = SocketError::Refused;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.Step();
    ASSERT_EQ(Harness::QuadsOf(tested.Frame(), NoticeBoxSlot), 1u);
    EXPECT_FALSE(tested.scene->WantsToQuit());

    tested.Tap(Key::Enter);

    EXPECT_TRUE(tested.scene->WantsToQuit());
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el OK del aviso cierra el juego"));
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), NoticeBoxSlot), 0u);
}

// Otra versión en el saludo del GS: el aviso 0x6f, con sus dos textos (0x4c1e20), y su OK también
// cierra el juego.
TEST(LoginSceneSessionTest, AnotherVersionShowsItsNotice)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ChooseTheLabServer());
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.Step();
    tested.Step();
    tested.socket.reads = {GameHello(1, "09908")};
    tested.Step();

    ASSERT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    const std::vector<FrameEvent> events = tested.Frame();
    EXPECT_EQ(Harness::QuadsOf(events, LoginBackgroundSlot), 0u);
    EXPECT_TRUE(Harness::Drew(events, VersionFirstText));
    EXPECT_TRUE(Harness::Drew(events, VersionSecondText));

    tested.Tap(Key::Escape);
    EXPECT_TRUE(tested.scene->WantsToQuit());
}

// Entrar otra vez olvida el pedido de cerrar el juego.
TEST(LoginSceneSessionTest, EnteringAgainForgetsTheQuit)
{
    Harness tested;
    tested.socket.connectResult = SocketError::Refused;
    ASSERT_TRUE(tested.scene->Enter().HasValue());
    tested.Step();
    tested.Tap(Key::Enter);
    ASSERT_TRUE(tested.scene->WantsToQuit());

    ASSERT_TRUE(tested.scene->Enter().HasValue());

    EXPECT_FALSE(tested.scene->WantsToQuit());
}

// Cancelar en la cuenta (0x642810): la sesión vuelve a empezar con el CS, y con su lista vuelve la
// ventana de la lista.
TEST(LoginSceneSessionTest, CancelGoesBackToTheList)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());

    tested.Tap(Key::Escape);

    EXPECT_EQ(tested.socket.connects.size(), 3u);
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), LoginBackgroundSlot), 0u);
    tested.socket.reads = {Hello()};
    tested.Step();
    tested.socket.reads = {ServerList({400})};
    tested.Step();
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(Harness::QuadsOf(tested.Frame(), GroupButtonSlot), 1u);
    EXPECT_FALSE(tested.scene->WantsToQuit());
}

// Con el login aceptado, la selección, como en la CL5.4.
TEST(LoginSceneSessionTest, LoggedInGoesToCharacterSelect)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.TypeTheCredentials();
    EXPECT_EQ(tested.scene->Next(), std::nullopt);

    tested.socket.reads = {LoginResult(1)};
    tested.Step();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.scene->Next(), SceneId::CharacterSelect);
}

// Las ventanas siguen la pantalla del último frame: en 1024 × 768 la cuenta va donde dice
// LayoutLogin para 1024 × 768, y un clic en su cancelar, también.
TEST(LoginSceneSessionTest, TheWindowsFollowTheScreenOfTheLastFrame)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheLogin());
    tested.target.SetScreen(PixelSize{1024, 768});
    const std::vector<FrameEvent> events = tested.Frame();
    const LoginLayout layout = LayoutLogin(PixelSize{1024, 768});
    const auto background =
        std::ranges::find_if(events, [](const FrameEvent& event) { return event.texture == LoginBackgroundSlot; });
    ASSERT_NE(background, events.end());
    // La esquina de arriba a la izquierda, con la y desde abajo (UiImage).
    EXPECT_EQ(background->vertices[0].x, static_cast<float>(layout.window.x));
    EXPECT_EQ(background->vertices[0].y, static_cast<float>(768 - layout.window.y));

    tested.ClickOn(layout.cancel);

    EXPECT_EQ(tested.socket.connects.size(), 3u);
}
} // namespace
