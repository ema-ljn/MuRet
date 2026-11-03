#include "Scenes/LoginScene.h"

#include "App/AutoLogin.h"
#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/TextureSets.h"
#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeServers.h"
#include "support/FakeTextureStore.h"
#include "support/LoopbackServer.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Del login a la selección de punta a punta (CL5.4): el SceneManager con la LoginScene de verdad,
// la ServerSession sobre sockets de verdad y los FakeServers de la CL4.4 en 127.0.0.1, todo en un
// hilo, como test_server_session_loopback.cpp. La LoginScene conecta al entrar, el SceneManager
// corre la sesión en cada Poll y, con el login aceptado, la escena pide la selección. La elección y
// la cuenta las da el AutoLogin (CL5.5), como Main con --auto-login: la escena no abre sus
// ventanas, porque nunca ve la sesión esperando.
//
// Solo la cuenta y la clave sintéticas de MuClientNetFixtures.h, y ningún mensaje trae un byte de
// lo que pasó por los sockets.
namespace
{
using Client::App::AutoLogin;
using Client::App::Credentials;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Network::Connection;
using Client::Network::FoxAntiObfuscation;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
using Client::Scenes::CharacterSelectScene;
using Client::Scenes::GameData;
using Client::Scenes::IScene;
using Client::Scenes::LoginScene;
using Client::Scenes::LoginTextureSet;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneId;
using Client::Scenes::SceneManager;
using Client::Scenes::WindowTextureSet;
using Client::Tests::FakeClock;
using Client::Tests::FakeServers;
using Client::Tests::FakeTextureStore;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerKeys;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::RetryUntil;
using Client::Tests::TemporaryFolder;
using Client::Tests::WriteTextureSet;

using Lines = std::vector<std::string>;

constexpr std::string_view Loopback = "127.0.0.1";
constexpr std::uint32_t PaddingSeed = 1;
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

TEST(LoginSceneLoopbackTest, GoesToTheSelectionWhenTheLoginIsAccepted)
{
    // En este orden: lo que tiene un socket muere antes que el NetworkInit.
    const NetworkInit network;
    ASSERT_TRUE(network.IsReady()) << "WSAStartup: " << network.SystemError();
    FakeServers servers(GameServerKeys());
    ASSERT_NE(servers.ConnectServerPort(), 0);
    SystemResolver resolver;
    SystemSocket socket(resolver);
    FakeClock clock;
    clock.Advance(std::chrono::milliseconds{123456});
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const FoxAntiObfuscation foxAnti;
    Connection connection(socket, foxAnti, clock, log, PaddingSeed);
    ServerSession session(connection, clock, log, GameClientKeys(),
                          SessionSettings{ServerAddress{std::string(Loopback), servers.ConnectServerPort()}});
    // Como Main con --auto-login: después de la sesión, en el mismo pollNetwork (CL5.5).
    AutoLogin autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log);
    TemporaryFolder folder{"login-scene-loopback"};
    WriteTextureSet(folder.Path(), LoginTextureSet);
    WriteTextureSet(folder.Path(), WindowTextureSet);
    FakeTextureStore store;
    const GameData data;
    RecordingFrameTarget target;
    std::vector<SceneId> built;
    SceneManager manager(
        [&](SceneId id) -> std::unique_ptr<IScene>
        {
            built.push_back(id);
            if (id == SceneId::Login)
            {
                return std::make_unique<LoginScene>(SceneContext{folder.Path(), store, log}, data, &session, "0.0.0");
            }
            if (id == SceneId::CharacterSelect)
            {
                return std::make_unique<CharacterSelectScene>();
            }
            return nullptr;
        },
        target, clock,
        [&session, &autoLogin]
        {
            session.Poll();
            autoLogin.Poll();
        },
        log);
    const InputState input;

    ASSERT_TRUE(manager.Start(SceneId::Login).HasValue());
    EXPECT_EQ(session.State(), SessionState::ConnectingConnectServer);
    const bool settled = RetryUntil(
        [&]
        {
            servers.Poll();
            manager.Poll();
            manager.Step(input);
            return manager.Current() != SceneId::Login || session.State() == SessionState::Failed;
        });

    EXPECT_TRUE(settled) << "la sesión quedó en " << ::testing::PrintToString(session.State());
    EXPECT_EQ(servers.Complaints(), Lines{});
    EXPECT_EQ(session.State(), SessionState::LoggedIn);
    EXPECT_EQ(manager.Current(), SceneId::CharacterSelect);
    EXPECT_EQ(built, (std::vector<SceneId>{SceneId::Login, SceneId::CharacterSelect}));
    // El login soltó sus 21 al salir, y la sesión sigue: es de main.cpp, no de la escena.
    EXPECT_EQ(store.released.size(), LoginTextureSet.size() + WindowTextureSet.size());
    EXPECT_EQ(store.Size(), 0u);
    EXPECT_TRUE(std::ranges::none_of(
        sink.lines, [](const std::string& line)
        { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; }));
}
} // namespace
