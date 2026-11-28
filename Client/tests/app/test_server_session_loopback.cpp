#include "Session/ServerSession.h"

#include "App/AutoLogin.h"
#include "Session/Credentials.h"
#include "Kernel/FaithfulPacer.h"
#include "App/LabXorRemapTables.h"
#include "Session/ServerAddress.h"
#include "Session/SessionState.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Network/Xor32Packet.h"
#include "Network/XorRemap/XorRemapObfuscation.h"
#include "Protocol/FrontEnd/CharacterSelect.h"
#include "Protocol/Session/Login.h"

#include "support/AppPrinters.h"
#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/FakeServers.h"
#include "support/LoopbackServer.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::App::ServerSession end to end, with real sockets (the CL4 plan, CL4.4): SystemSocket, Connection with
// FoxAnti and the session, against the FakeServers on 127.0.0.1, which follow the 04 script with the wire bytes and
// the synthetic keys. All on one thread: the test polls the servers and the session in the same loop, up to 5 real s
// (RetryUntil), and what the system does in the background does not change the result, because each side answers
// what reached it. The session's clock is a FakeClock: it gives the login tick, and the connect does not time out.
// The session waits for the choice and the account (CL5.5): the AutoLogin gives them, as with --auto-login.
//
// Only the synthetic account and password of MuClientNetFixtures.h; the login is compared with
// SameBytesWithoutValues, and no message carries a byte of what went through the sockets.
namespace
{
using Client::App::AutoLogin;
using Client::App::Credentials;
using Client::App::LabXorRemapTables;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Kernel::TickCountMs;
using Client::Network::Connection;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::Xor32Decode;
using Client::Network::SocketApi::NetworkInit;
using Client::Network::XorRemap::XorRemapObfuscation;
using Client::Protocol::FrontEnd::WriteCharacterSelect;
using Client::Protocol::Session::LoginRequest;
using Client::Protocol::Session::WriteLoginRequest;
using Client::Session::ServerAddress;
using Client::Session::SessionFailure;
using Client::Session::SessionState;
using Client::Tests::FakeClock;
using Client::Tests::FakeServers;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerKeys;
using Client::Tests::NameBytes;
using Client::Tests::RecordingSink;
using Client::Tests::RetryUntil;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;

using Lines = std::vector<std::string>;

constexpr std::string_view Loopback = "127.0.0.1";
constexpr std::uint32_t PaddingSeed = 1;
constexpr std::uint16_t LabServerCode = 400; // The server FakeServers announces.

// The synthetic account and password of the login fixtures (MuClientNetFixtures.h).
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

// The log lines of `level` or more severe.
Lines LinesFrom(const RecordingSink& sink, LogLevel level)
{
    Lines found;
    for (std::size_t index = 0; index < sink.lines.size(); ++index)
    {
        if (sink.levels[index] >= level)
        {
            found.push_back(sink.lines[index]);
        }
    }
    return found;
}

// The two fake servers and a session that talks to them over real sockets, with the AutoLogin that chooses and sends
// the account. In this order: what holds a socket dies before the NetworkInit.
struct LoopbackSession
{
    explicit LoopbackSession(Client::Core::Duration idleTimeout = Client::Core::Duration::zero())
        : servers(GameServerKeys(), clock, idleTimeout), socket(resolver),
          connection(socket, xorRemap, clock, log, PaddingSeed),
          session(connection, clock, log, GameClientKeys(),
                  SessionSettings{ServerAddress{std::string(Loopback), servers.ConnectServerPort()}}, eventSink),
          autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log)
    {
        clock.Advance(std::chrono::milliseconds{123456});
    }

    // Manual choice without sending credentials: PollUntil does not poll the AutoLogin, which only LogIn uses. The
    // player leaves the login form open.
    [[nodiscard]] bool OpenLoginForm()
    {
        session.Start();
        if (!PollUntil([&] { return session.State() == SessionState::ChoosingServer; }) ||
            session.State() != SessionState::ChoosingServer)
        {
            return false;
        }
        session.ChooseServer(LabServerCode);
        return PollUntil([&] { return session.State() == SessionState::WaitingCredentials; }) &&
               session.State() == SessionState::WaitingCredentials;
    }

    // Start, then both sides in the same loop until LoggedIn or Failed.
    [[nodiscard]] bool LogIn()
    {
        session.Start();
        return RetryUntil(
            [&]
            {
                servers.Poll();
                static_cast<void>(session.Poll());
                autoLogin.Poll();
                return session.State() == SessionState::LoggedIn || session.State() == SessionState::Failed;
            });
    }

    // Both sides without the AutoLogin in the same loop until `done` or a complaint from the server.
    template <typename Done> [[nodiscard]] bool PollUntil(Done done)
    {
        return RetryUntil(
            [&]
            {
                servers.Poll();
                static_cast<void>(session.Poll());
                return done() || !servers.Complaints().empty() || session.State() == SessionState::Failed;
            });
    }

    NetworkInit network;
    FakeClock clock;
    FakeServers servers;
    SystemResolver resolver;
    SystemSocket socket;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    XorRemapObfuscation xorRemap{LabXorRemapTables()};
    Connection connection;
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session;
    AutoLogin autoLogin;
};

// Control for the double: a client that stops polling sends no 0E, and the GS with a synthetic deadline really
// closes the socket. The deadline is the test's, not a measurement of the lab's GS.
TEST(ServerSessionLoopbackTest, AnIdleGameServerClosesWithoutKeepalives)
{
    constexpr std::chrono::seconds IdleTimeout{30};
    LoopbackSession tested{IdleTimeout};
    ASSERT_TRUE(tested.network.IsReady());
    ASSERT_TRUE(tested.OpenLoginForm());
    ASSERT_FALSE(tested.servers.SawLogin());

    tested.clock.Advance(IdleTimeout);
    tested.servers.Poll(); // expires before the client polls again

    EXPECT_TRUE(tested.servers.IdleExpired());
    EXPECT_EQ(tested.servers.Keepalives(), 0u);
    ASSERT_TRUE(tested.PollUntil([&] { return tested.session.State() == SessionState::Failed; }));
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_EQ(tested.servers.Complaints(), Lines{});
}

// Two minutes without typing: the same GS that times out in the control keeps the connection thanks to encrypted
// 0E. The later login must continue their serials, and gets an immediate 0E.
TEST(ServerSessionLoopbackTest, KeepalivesPreserveTheLoginFormUntilCredentialsAreSubmitted)
{
    constexpr std::chrono::seconds IdleTimeout{30};
    constexpr std::chrono::seconds TickPeriod{20};
    constexpr std::size_t IdleTicks = 6;
    LoopbackSession tested{IdleTimeout};
    ASSERT_TRUE(tested.network.IsReady());
    ASSERT_TRUE(tested.OpenLoginForm());

    for (std::size_t tick = 1; tick <= IdleTicks; ++tick)
    {
        tested.clock.Advance(TickPeriod);
        ASSERT_TRUE(tested.PollUntil([&] { return tested.servers.Keepalives() >= tick; }))
            << "no llegó el keepalive durante la espera humana " << tick;
        ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
        ASSERT_FALSE(tested.servers.IdleExpired());
        ASSERT_FALSE(tested.servers.SawLogin());
        ASSERT_FALSE(tested.servers.Login().has_value());
        ASSERT_EQ(tested.servers.Complaints(), Lines{});
    }

    tested.session.SubmitCredentials(Credentials{std::string(Account), std::string(Password)});
    ASSERT_TRUE(tested.PollUntil([&] { return tested.session.State() == SessionState::LoggedIn; }));
    ASSERT_EQ(tested.session.State(), SessionState::LoggedIn);
    ASSERT_TRUE(tested.servers.SawLogin());
    ASSERT_TRUE(tested.PollUntil([&] { return tested.servers.Keepalives() > IdleTicks; }));
    EXPECT_EQ(tested.servers.Keepalives(), IdleTicks + 1);
    EXPECT_FALSE(tested.servers.IdleExpired());
    EXPECT_EQ(tested.servers.Complaints(), Lines{});
    EXPECT_EQ(LinesFrom(tested.sink, LogLevel::Warning), Lines{});
}

// From Start to LoggedIn: the CS with the A9, the hello, the lab list that FoxAnti turns into F4 06, the choice that
// FoxAnti turns into code 0 and the fake GS's address; the GS with the hello, the login encrypted with serial 0 and
// the result. Then the GS gets the first keepalive, with serial 1 (CL4.5), and the CS sees that the client hung up.
TEST(ServerSessionLoopbackTest, LogsInAgainstFakeServers)
{
    LoopbackSession tested;
    ASSERT_TRUE(tested.network.IsReady()) << "WSAStartup: " << tested.network.SystemError();
    ASSERT_NE(tested.servers.ConnectServerPort(), 0);
    ASSERT_NE(tested.servers.GameServerPort(), 0);

    const bool settled = tested.LogIn();

    EXPECT_TRUE(settled) << "la sesión quedó en " << ::testing::PrintToString(tested.session.State());
    EXPECT_EQ(tested.servers.Complaints(), Lines{});
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_TRUE(tested.servers.SawLogin());
    const std::optional<Client::Network::Packet>& login = tested.servers.Login();
    if (!login)
    {
        FAIL() << "el GameServer no vio el login";
    }
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    EXPECT_TRUE(SameBytesWithoutValues(login->Bytes(), expected.Value().packet.Bytes()));

    EXPECT_TRUE(tested.PollUntil([&] { return tested.servers.Keepalives() >= 1; }))
        << "el GameServer no vio el primer keepalive";
    EXPECT_EQ(tested.servers.Keepalives(), 1u);
    EXPECT_TRUE(RetryUntil(
        [&]
        {
            tested.servers.Poll();
            return tested.servers.ClientLeftTheConnectServer();
        }))
        << "el ConnectServer no vio que el cliente cortó";
    EXPECT_EQ(tested.servers.Complaints(), Lines{});
    EXPECT_EQ(LinesFrom(tested.sink, LogLevel::Warning), Lines{});
    EXPECT_TRUE(std::ranges::none_of(
        tested.sink.lines, [](const std::string& line)
        { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; }));
}

// From the login to the entry (CL6.3): the F3 00 in clear, the fake GS's list in clear and with FoxAnti's 0xAA, the
// choice of slot 2 and the F3 03 with that one's name, char0002.
TEST(ServerSessionLoopbackTest, FromLoginToEnterWorld)
{
    LoopbackSession tested;
    ASSERT_TRUE(tested.network.IsReady()) << "WSAStartup: " << tested.network.SystemError();
    ASSERT_TRUE(tested.LogIn()) << "la sesión quedó en " << ::testing::PrintToString(tested.session.State());
    ASSERT_EQ(tested.session.State(), SessionState::LoggedIn);

    tested.session.RequestCharacterList();
    ASSERT_TRUE(tested.PollUntil([&] { return tested.session.State() == SessionState::ChoosingCharacter; }))
        << "la sesión quedó en " << ::testing::PrintToString(tested.session.State());
    ASSERT_EQ(tested.servers.Complaints(), Lines{});
    ASSERT_TRUE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    tested.session.ChooseCharacter(std::uint8_t{2});
    tested.session.EnterWorld();
    ASSERT_TRUE(tested.PollUntil([&] { return tested.servers.CharacterSelect().has_value(); }))
        << "el GameServer no vio el F3 03";

    EXPECT_EQ(tested.servers.Complaints(), Lines{});
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    const std::optional<Client::Network::Packet>& select = tested.servers.CharacterSelect();
    if (!select)
    {
        FAIL() << "el GameServer no vio el F3 03";
    }
    EXPECT_TRUE(SameBytes(select->Bytes(), Xor32Decode(WriteCharacterSelect(NameBytes("char0002")).packet).Bytes()));
    EXPECT_EQ(LinesFrom(tested.sink, LogLevel::Warning), Lines{});
}
} // namespace
