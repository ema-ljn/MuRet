#include "App/AutoLogin.h"

#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Network/PacketSerials.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Login.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::App::AutoLogin (CL5.5): lo que la ServerSession de la CL4 hacía sola, ahora para Main
// con --auto-login. Elige el primero de la lista, en el orden en que llegó (pregunta 9 del plan de
// la CL4), y manda la cuenta y la clave del entorno (pregunta 7), una vez cada una: un servidor
// ocupado o un login rechazado quedan para la UI. Sobre una sesión de verdad y un FakeSocket, con
// la cuenta y la clave sintéticas.
namespace
{
using Client::App::AutoLogin;
using Client::App::Credentials;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::App::TickCountMs;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::Connection;
using Client::Network::IdentityObfuscation;
using Client::Network::PacketSerials;
using Client::Protocol::ClientVersion;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::WriteLoginRequest;
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
          session(connection, clock, log, GameClientKeys(), SessionSettings{ServerAddress{"10.0.0.1", 44405}}),
          autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log)
    {
    }

    // Lo de main.cpp en cada frame: la sesión y, después, el AutoLogin.
    void Poll()
    {
        session.Poll();
        autoLogin.Poll();
    }

    // Start, el saludo del CS y la lista `list`.
    void ReachTheList(const ByteVector& list)
    {
        session.Start();
        socket.reads = {Hello()};
        Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
        socket.reads = {list};
        Poll();
    }

    // Y la dirección del GS, su Opened y su saludo.
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
    ServerSession session;
    AutoLogin autoLogin;
    std::size_t gameServerStart = 0;
    PacketSerials gameServerSends;
};

// El primero de la lista en el orden en que llegó (el 401, no el 400), y la cuenta y la clave en
// cuanto el GS saluda: el F4 03 con 91 01 y el login, que se compara sin sus valores.
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

// Mientras la sesión no espera nada, el AutoLogin no hace nada: sin Start, y conectando.
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

// Con el F4 05 la sesión vuelve a pedir la lista (Q-C71); el AutoLogin no elige otra vez el mismo,
// que seguiría ocupado: la lista queda para la UI, con un aviso, uno solo.
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

// Un login rechazado vuelve a WaitingCredentials (CL5.5): el AutoLogin no manda la misma cuenta y
// la misma clave otra vez. Queda un aviso, uno solo, y la sesión espera a la UI.
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

// Un Start nuevo (el cancelar de la cuenta, 0x642810) es otra vuelta: el AutoLogin elige y manda
// otra vez.
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
