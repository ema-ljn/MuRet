#include "support/FakeServers.h"

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
