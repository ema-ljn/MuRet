#include "App/SessionClient.h"

#include "App/Credentials.h"
#include "App/ILoopClient.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"

#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingClient.h"
#include "support/RecordingSink.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

// Client::App::SessionClient: el cliente del bucle de Main con un servidor (el plan de la CL4,
// CL4.5). La sesión va sobre una Connection de verdad y un FakeSocket, sin servidor: alcanza con
// que abra el ConnectServer.
namespace
{
using Client::App::Credentials;
using Client::App::ILoopClient;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionClient;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Network::Connection;
using Client::Network::IdentityObfuscation;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::GameClientKeys;
using Client::Tests::RecordingClient;
using Client::Tests::RecordingSink;

constexpr std::uint32_t PaddingSeed = 1;

// Una escena que anota en qué estado estaba la sesión en cada Poll.
class WatchingScene final : public ILoopClient
{
public:
    explicit WatchingScene(const ServerSession& session) : m_session(session) {}

    void Poll() override
    {
        seen.push_back(m_session.State());
    }
    void Step(const InputState& /*input*/) override {}
    void Render(float /*alpha*/) override {}

    std::vector<SessionState> seen;

private:
    const ServerSession& m_session;
};

// Una sesión empezada, con el ConnectServer que conecta en el primer Poll.
struct Started
{
    Started()
        : connection(socket, IdentityObfuscation::Shared(), clock, log, PaddingSeed),
          session(connection, clock, log, GameClientKeys(),
                  SessionSettings{ServerAddress{"10.0.0.1", 44405}, Credentials{"user0001", "pass0002"}})
    {
        session.Start();
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    ServerSession session;
};

// Primero la sesión y después la escena: en el Poll de la escena, la sesión ya leyó el connect y
// mandó el A9.
TEST(SessionClientTest, PollsTheSessionAndThenTheScene)
{
    Started started;
    WatchingScene scene(started.session);
    SessionClient client(started.session, scene);

    client.Poll();

    EXPECT_EQ(scene.seen, std::vector<SessionState>{SessionState::WaitingConnectHello});
    EXPECT_FALSE(started.socket.sent.empty());
}

// Los pasos y el dibujo son de la escena, y no consultan la sesión.
TEST(SessionClientTest, TheStepsAndTheDrawingAreTheScenes)
{
    Started started;
    RecordingClient scene{started.clock};
    SessionClient client(started.session, scene);
    const InputState input;

    client.Step(input);
    client.Render(0.5f);

    EXPECT_EQ(scene.steps, 1u);
    EXPECT_EQ(scene.alphas, std::vector<float>{0.5f});
    EXPECT_EQ(scene.polls, 0u);
    EXPECT_EQ(started.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(started.socket.sent.empty());
}
} // namespace
