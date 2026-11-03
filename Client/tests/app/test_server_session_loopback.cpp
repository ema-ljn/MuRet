#include "App/ServerSession.h"

#include "App/AutoLogin.h"
#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/Packet.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Protocol/Connection/Login.h"

#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeServers.h"
#include "support/LoopbackServer.h"
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

// Client::App::ServerSession de punta a punta, con sockets de verdad (el plan de la CL4, CL4.4,
// PR C): SystemSocket, Connection con FoxAnti y la sesión, contra los FakeServers en 127.0.0.1, que
// siguen el guion del 04 con los bytes del cable y las claves sintéticas. Todo en un hilo: el test
// consulta a los servidores y a la sesión en el mismo lazo, hasta 5 s reales (RetryUntil), y lo
// que el sistema hace en el fondo no cambia el resultado, porque cada lado contesta lo que le
// llegó. El reloj de la sesión es un FakeClock: da el tick del login, y el connect no vence. La
// sesión espera la elección y la cuenta (CL5.5): las da el AutoLogin, como con --auto-login.
//
// Solo la cuenta y la clave sintéticas de MuClientNetFixtures.h; el login se compara con
// SameBytesWithoutValues, y ningún mensaje trae un byte de lo que pasó por los sockets.
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
using Client::Network::FoxAntiObfuscation;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::WriteLoginRequest;
using Client::Tests::FakeClock;
using Client::Tests::FakeServers;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerKeys;
using Client::Tests::RecordingSink;
using Client::Tests::RetryUntil;
using Client::Tests::SameBytesWithoutValues;

using Lines = std::vector<std::string>;

constexpr std::string_view Loopback = "127.0.0.1";
constexpr std::uint32_t PaddingSeed = 1;

// La cuenta y la clave sintéticas de los fixtures del login (MuClientNetFixtures.h).
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

// Las líneas del log de `level` o más graves.
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

// Del Start a LoggedIn: el CS con el A9, el saludo, la lista del laboratorio que FoxAnti pasa a
// F4 06, la elección que FoxAnti pasa al código 0 y la dirección del GS falso; el GS con el
// saludo, el login cifrado con el serial 0 y el resultado. Después, el GS recibe el primer
// keepalive, con el serial 1 (CL4.5), y el CS ve que el cliente cortó.
TEST(ServerSessionLoopbackTest, LogsInAgainstFakeServers)
{
    // En este orden: lo que tiene un socket muere antes que el NetworkInit.
    const NetworkInit network;
    ASSERT_TRUE(network.IsReady()) << "WSAStartup: " << network.SystemError();
    FakeServers servers(GameServerKeys());
    ASSERT_NE(servers.ConnectServerPort(), 0);
    ASSERT_NE(servers.GameServerPort(), 0);
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
    AutoLogin autoLogin(session, Credentials{std::string(Account), std::string(Password)}, log);

    session.Start();
    const bool settled = RetryUntil(
        [&]
        {
            servers.Poll();
            session.Poll();
            autoLogin.Poll();
            return session.State() == SessionState::LoggedIn || session.State() == SessionState::Failed;
        });

    EXPECT_TRUE(settled) << "la sesión quedó en " << ::testing::PrintToString(session.State());
    EXPECT_EQ(servers.Complaints(), Lines{});
    EXPECT_EQ(session.State(), SessionState::LoggedIn);
    EXPECT_EQ(session.Failure(), std::nullopt);
    EXPECT_TRUE(servers.SawLogin());
    const std::optional<Client::Network::Packet>& login = servers.Login();
    if (!login)
    {
        FAIL() << "el GameServer no vio el login";
    }
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    EXPECT_TRUE(SameBytesWithoutValues(login->Bytes(), expected.Value().packet.Bytes()));

    EXPECT_TRUE(RetryUntil(
        [&]
        {
            servers.Poll();
            session.Poll();
            return servers.Keepalives() >= 1 || !servers.Complaints().empty();
        }))
        << "el GameServer no vio el primer keepalive";
    EXPECT_EQ(servers.Keepalives(), 1u);
    EXPECT_TRUE(RetryUntil(
        [&]
        {
            servers.Poll();
            return servers.ClientLeftTheConnectServer();
        }))
        << "el ConnectServer no vio que el cliente cortó";
    EXPECT_EQ(servers.Complaints(), Lines{});
    EXPECT_EQ(LinesFrom(sink, LogLevel::Warning), Lines{});
    EXPECT_TRUE(std::ranges::none_of(
        sink.lines, [](const std::string& line)
        { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; }));
}
} // namespace
