#include "App/AutoLogin.h"

#include "Session/Credentials.h"
#include "Kernel/FaithfulPacer.h"
#include "Session/ServerAddress.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Network/PacketSerials.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/Session/Login.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::App::AutoLogin (CL5.5), for Main with --auto-login. It chooses the first of the list, in the order it
// arrived (question 9 of the CL4 plan), and sends the account and the password from the environment (question 7),
// once each: a busy server or a rejected login is left to the UI. On a real session and a FakeSocket, with a
// synthetic account and password.
namespace
{
using Client::App::AutoLogin;
using Client::App::Credentials;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Kernel::TickCountMs;
using Client::Network::Connection;
using Client::Network::IdentityObfuscation;
using Client::Network::PacketSerials;
using Client::Protocol::Session::ClientVersion;
using Client::Protocol::Session::LoginRequest;
using Client::Protocol::Session::WriteLoginRequest;
using Client::Session::ServerAddress;
using Client::Session::SessionState;
using Client::Tests::AddressReply;
using Client::Tests::BusyReply;
using Client::Tests::Bytes;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameClientKeys;
using Client::Tests::GameHello;
using Client::Tests::GameServerReceived;
using Client::Tests::Hello;
using Client::Tests::Join;
using Client::Tests::ListRequest;
using Client::Tests::LoginResult;
using Client::Tests::PetInfoRequest;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::ServerList;

using ByteVector = std::vector<std::byte>;

constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

struct Harness
{
    Harness()
        : connection(socket, IdentityObfuscation::Shared(), clock, log, 1),
          session(connection, clock, log, GameClientKeys(), SessionSettings{ServerAddress{"10.0.0.1", 44405}},
                  eventSink),
          autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log)
    {
    }

    // What main.cpp does each frame: the session and then the AutoLogin.
    void Poll()
    {
        const auto polled = session.Poll();
        if (!polled.HasValue())
        {
            return;
        }
        autoLogin.Poll();
    }

    // Start, the CS hello and the list `list`.
    void ReachTheList(const ByteVector& list)
    {
        session.Start();
        socket.reads = {Hello()};
        Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
        socket.reads = {list};
        Poll();
    }

    // Then the GS address, its Opened and its hello.
    void ReachTheGameHello()
    {
        socket.reads = {AddressReply("10.0.0.2", 55901)};
        Poll();
        gameServerStart = socket.sent.size();
        Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingGameHello);
        socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), gameServerSends)};
        Poll();
    }

    [[nodiscard]] GameServerReceived GameServerReads() const
    {
        PacketSerials serials;
        return Client::Tests::AsTheGameServerReceives(std::span(socket.sent).subspan(gameServerStart), serials);
    }

    [[nodiscard]] std::size_t CountLogged(LogLevel level, std::string_view part) const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool LoggedTheCredentials() const
    {
        return std::ranges::any_of(
            sink.lines, [](const std::string& line)
            { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; });
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session;
    AutoLogin autoLogin;
    std::size_t gameServerStart = 0;
    PacketSerials gameServerSends;
};

// The first of the list in the order it arrived (401, not 400), and the account and the password as soon as the GS
// says hello: the F4 03 with 91 01 and the login, compared without its values.
TEST(AutoLoginTest, ChoosesTheFirstAndSubmitsTheEnvironment)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({401, 400})));

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x91, 0x01})})));

    ASSERT_NO_FATAL_FAILURE(tested.ReachTheGameHello());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), expected.Value().packet.Bytes()));
    EXPECT_FALSE(tested.LoggedTheCredentials());

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Without Start, and while connecting.
TEST(AutoLoginTest, DoesNothingWhileTheSessionDoesNotWait)
{
    Harness tested;
    tested.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::Idle);

    tested.socket.pollsUntilConnected = 3;
    tested.session.Start();
    tested.Poll();
    tested.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.socket.sent.empty());

    tested.Poll();
    tested.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, PetInfoRequest()));
}

// With the F4 05 the session asks for the list again (Q-C71); the AutoLogin does not choose the same one again,
// which would still be busy: the list is left to the UI, with one notice only.
TEST(AutoLoginTest, ABusyServerIsNotChosenAgain)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400})));
    tested.socket.reads = {BusyReply()};
    tested.Poll();
    tested.socket.reads = {ServerList({400})};
    tested.Poll();
    const ByteVector sent = tested.socket.sent;

    tested.Poll();
    tested.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "--auto-login ya eligió"), 1u);
}

// A rejected login goes back to WaitingCredentials (CL5.5). One notice only, and the session waits for the UI.
TEST(AutoLoginTest, ARejectedLoginIsNotSubmittedAgain)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400})));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheGameHello());
    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    tested.Poll();

    tested.Poll();
    tested.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "--auto-login ya mandó"), 1u);
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// A new Start (the account's cancel, 0x642810) is another round: the AutoLogin chooses and sends again.
TEST(AutoLoginTest, AStartAgainIsANewRound)
{
    Harness tested;
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({400})));
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheGameHello());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    ASSERT_NO_FATAL_FAILURE(tested.ReachTheList(ServerList({401})));
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{std::uint16_t{401}});
    ASSERT_NO_FATAL_FAILURE(tested.ReachTheGameHello());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "--auto-login"), 0u);
}
} // namespace
