#include "Scenes/LoginScene.h"

#include "App/AutoLogin.h"
#include "Session/Credentials.h"
#include "App/LabXorRemapTables.h"
#include "Session/ServerAddress.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Network/XorRemap/XorRemapObfuscation.h"
#include "Tables/GameData.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Assets/TextureSets.h"
#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/IdleScene.h"
#include "support/FakeServers.h"
#include "support/FakeTextureStore.h"
#include "support/LoopbackServer.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"
#include "FrontEnd/LoginModels.h"
#include "support/FixedRandom.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// From the login to the selection end to end (CL5.4): the SceneManager with the real LoginScene, the ServerSession on
// real sockets and CL4.4's FakeServers on 127.0.0.1, all on one thread, like test_server_session_loopback.cpp. The
// LoginScene connects on entering, the SceneManager runs the session on each Poll and, with the login accepted, the
// scene asks for the selection. The choice and the account come from AutoLogin (CL5.5), like Main with --auto-login:
// the scene does not open its windows, because it never sees the session waiting.
//
// Only the synthetic account and password of MuClientNetFixtures.h, and no message carries a byte of what went
// through the sockets.
namespace
{
using Client::App::AutoLogin;
using Client::App::Credentials;
using Client::App::LabXorRemapTables;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::Assets::LoginTextureSet;
using Client::Assets::WindowTextureSet;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Kernel::SceneId;
using Client::Network::Connection;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
using Client::Network::XorRemap::XorRemapObfuscation;
using Client::Scenes::IScene;
using Client::Scenes::LoginScene;
using Client::Scenes::SceneContext;
using Client::Scenes::SceneManager;
using Client::Session::ServerAddress;
using Client::Session::SessionState;
using Client::Tables::GameData;
using Client::Tests::FakeClock;
using Client::Tests::FakeServers;
using Client::Tests::FakeTextureStore;
using Client::Tests::FixedSeed;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerKeys;
using Client::Tests::IdleScene;
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
    // In this order: whatever holds a socket dies before the NetworkInit.
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
    const XorRemapObfuscation xorRemap{LabXorRemapTables()};
    Connection connection(socket, xorRemap, clock, log, PaddingSeed);
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session(connection, clock, log, GameClientKeys(),
                          SessionSettings{ServerAddress{std::string(Loopback), servers.ConnectServerPort()}},
                          eventSink);
    // Like Main with --auto-login: after the session, in the same pollNetwork (CL5.5).
    AutoLogin autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log);
    TemporaryFolder folder{"login-scene-loopback"};
    WriteTextureSet(folder.Path(), LoginTextureSet);
    WriteTextureSet(folder.Path(), WindowTextureSet);
    FakeTextureStore store;
    const GameData data;
    const Client::FrontEnd::SingleModelLibrary models;
    RecordingFrameTarget target;
    std::vector<SceneId> built;
    SceneManager manager(
        [&](SceneId id) -> std::unique_ptr<IScene>
        {
            built.push_back(id);
            if (id == SceneId::Login)
            {
                return std::make_unique<LoginScene>(SceneContext{folder.Path(), store, log}, data, models, &session,
                                                    "0.0.0", FixedSeed);
            }
            if (id == SceneId::CharacterSelect)
            {
                return std::make_unique<IdleScene>();
            }
            return nullptr;
        },
        target, clock,
        [&session, &autoLogin]
        {
            const auto polled = session.Poll();
            if (!polled.HasValue())
            {
                return polled;
            }
            autoLogin.Poll();
            return polled;
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
    // The login released its 21 on leaving, and the session goes on: it belongs to main.cpp, not to the scene.
    EXPECT_EQ(store.released.size(), LoginTextureSet.size() + WindowTextureSet.size());
    EXPECT_EQ(store.Size(), 0u);
    EXPECT_TRUE(std::ranges::none_of(
        sink.lines, [](const std::string& line)
        { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; }));
}
} // namespace
