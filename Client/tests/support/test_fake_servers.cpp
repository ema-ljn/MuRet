#include "support/FakeServers.h"

#include "Core/IClock.h"
#include "Network/ISocket.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/LoopbackServer.h"
#include "support/NetworkPrinters.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// support/FakeServers.h: lo que hacen con algo que no está en el guion del 04. El camino bueno lo
// recorre ServerSessionLoopbackTest con la sesión de verdad; acá, un SystemSocket a mano manda lo
// que no toca, y el servidor lo dice y corta.
namespace
{
using Client::Network::ConnectProgress;
using Client::Network::SocketError;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::FakeServers;
using Client::Tests::GameServerKeys;
using Client::Tests::RetryUntil;

using Lines = std::vector<std::string>;

constexpr std::chrono::seconds SyntheticIdleTimeout{30};

// Hace vencer el plazo entre el error del parser y su postproceso. La entrega del socket puede
// tardar lo que necesite: el reloj solo avanza cuando el doble ya anotó la queja. Así el test no
// depende de sleeps ni presenta este plazo sintético como una medición del GS original.
class DeadlineOnComplaintClock final : public Client::Core::IClock
{
public:
    Client::Core::Duration Now() override
    {
        return complaints && !complaints->empty() ? SyntheticIdleTimeout : Client::Core::Duration::zero();
    }

    void SleepFor(Client::Core::Duration) override
    {
        FAIL() << "el reloj de este test no duerme";
    }

    const Lines* complaints = nullptr;
};

TEST(FakeServersTest, AFramingFailureDoesNotBecomeAnIdleExpiry)
{
    const NetworkInit network;
    ASSERT_TRUE(network.IsReady()) << "WSAStartup: " << network.SystemError();
    DeadlineOnComplaintClock clock;
    FakeServers servers(GameServerKeys(), clock, SyntheticIdleTimeout);
    clock.complaints = &servers.Complaints();
    SystemResolver resolver;
    SystemSocket socket(resolver);
    const auto started = socket.StartConnect("127.0.0.1", servers.GameServerPort());
    ASSERT_TRUE(started.HasValue());
    ASSERT_TRUE(RetryUntil(
        [&]
        {
            const auto progress = socket.PollConnect();
            return progress.HasValue() && progress.Value() == ConnectProgress::Connected;
        }));

    const std::vector<std::byte> invalidHeader = Bytes({0x00});
    const auto sent = socket.Send(invalidHeader);
    ASSERT_TRUE(sent.HasValue());
    ASSERT_EQ(sent.Value(), invalidHeader.size());
    ASSERT_TRUE(RetryUntil(
        [&]
        {
            servers.Poll();
            return !servers.Complaints().empty();
        }));

    EXPECT_EQ(servers.Complaints(), Lines{"el GameServer no puede cortar lo que mandó el cliente"});
    EXPECT_FALSE(servers.IdleExpired());
    EXPECT_EQ(servers.Keepalives(), 0u);
    EXPECT_FALSE(servers.SawLogin());
}

// Lo primero que tiene que mandar el cliente al CS es el A9: un F4 02 antes no se contesta, queda
// en Complaints() y el CS corta, así el cliente ve el cierre.
TEST(FakeServersTest, TheConnectServerComplainsAboutWhatIsNotInTheScript)
{
    const NetworkInit network;
    ASSERT_TRUE(network.IsReady()) << "WSAStartup: " << network.SystemError();
    FakeServers servers(GameServerKeys());
    SystemResolver resolver;
    SystemSocket socket(resolver);
    const auto started = socket.StartConnect("127.0.0.1", servers.ConnectServerPort());
    ASSERT_TRUE(started.HasValue());
    bool connected = started.Value() == ConnectProgress::Connected;
    ASSERT_TRUE(RetryUntil(
        [&]
        {
            if (!connected)
            {
                const auto polled = socket.PollConnect();
                connected = polled.HasValue() && polled.Value() == ConnectProgress::Connected;
            }
            return connected;
        }));

    const std::vector<std::byte> listRequest = Bytes({0xC1, 0x04, 0xF4, 0x02});
    const auto sent = socket.Send(listRequest);
    ASSERT_TRUE(sent.HasValue());
    ASSERT_EQ(sent.Value(), listRequest.size());
    EXPECT_TRUE(RetryUntil(
        [&]
        {
            servers.Poll();
            return !servers.Complaints().empty();
        }));

    EXPECT_EQ(servers.Complaints(), Lines{"el ConnectServer esperaba el A9 del 04 y llegó otra cosa"});
    std::optional<SocketError> error;
    EXPECT_TRUE(RetryUntil(
        [&]
        {
            std::array<std::byte, 16> buffer{};
            error = ErrorOf(socket.Receive(buffer));
            return error.has_value();
        }));
    EXPECT_EQ(error, SocketError::Closed);
    EXPECT_FALSE(servers.SawLogin());
}
} // namespace
