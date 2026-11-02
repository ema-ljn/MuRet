#include "Network/SocketApi.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <utility>

// Client::Network::SocketApi: lo que no necesita una conexión. El RAII de NetworkInit y de
// UniqueSocket, y el tope de bytes por llamada de Winsock. Lo demás lo prueba SystemSocketTest
// contra un listener de verdad.
namespace
{
using Client::Network::SocketApi::BytesPerCall;
using Client::Network::SocketApi::ListenOnLoopback;
using Client::Network::SocketApi::LocalPort;
using Client::Network::SocketApi::MaxBytesPerCall;
using Client::Network::SocketApi::NetworkInit;
using Client::Network::SocketApi::OpenTcp;
using Client::Network::SocketApi::Status;
using Client::Network::SocketApi::UniqueSocket;

// recv y send de Winsock reciben el largo en un int.
TEST(SocketApiTest, EachCallAsksForAtMostIntMax)
{
    EXPECT_EQ(MaxBytesPerCall, static_cast<std::size_t>(std::numeric_limits<int>::max()));
    EXPECT_EQ(BytesPerCall(0), 0u);
    EXPECT_EQ(BytesPerCall(1024), 1024u);
    EXPECT_EQ(BytesPerCall(MaxBytesPerCall), MaxBytesPerCall);
    EXPECT_EQ(BytesPerCall(MaxBytesPerCall + 1), MaxBytesPerCall);
}

// WSAStartup cuenta las llamadas: el de adentro no apaga la red del de afuera.
TEST(SocketApiTest, NetworkInitCanBeNested)
{
    const NetworkInit outer;
    ASSERT_TRUE(outer.IsReady());
    {
        const NetworkInit inner;
        EXPECT_TRUE(inner.IsReady());
    }

    const auto opened = OpenTcp();
    EXPECT_EQ(opened.result.status, Status::Done);
    EXPECT_TRUE(opened.socket.IsOpen());
}

TEST(SocketApiTest, AnEmptyUniqueSocketIsNotOpen)
{
    UniqueSocket socket;

    EXPECT_FALSE(socket.IsOpen());
    socket.Reset();
    EXPECT_FALSE(socket.IsOpen());
}

// El que se movió queda vacío y su destructor no cierra el socket del otro, que sigue escuchando.
TEST(SocketApiTest, AMovedUniqueSocketIsEmptyAndTheOtherKeepsIt)
{
    const NetworkInit network;
    auto listener = ListenOnLoopback();
    ASSERT_EQ(listener.result.status, Status::Done);
    const auto handle = listener.socket.Get();
    const auto port = LocalPort(handle);
    ASSERT_NE(port, 0);

    UniqueSocket moved;
    {
        UniqueSocket first(std::move(listener.socket));
        EXPECT_FALSE(listener.socket.IsOpen());
        moved = std::move(first);
        EXPECT_FALSE(first.IsOpen());
    }

    EXPECT_EQ(moved.Get(), handle);
    EXPECT_EQ(LocalPort(moved.Get()), port);
}

TEST(SocketApiTest, ResetClosesAndEmpties)
{
    const NetworkInit network;
    auto opened = OpenTcp();
    ASSERT_TRUE(opened.socket.IsOpen());

    opened.socket.Reset();

    EXPECT_FALSE(opened.socket.IsOpen());
}

TEST(SocketApiTest, AnEmptyBufferDoesNotCallTheSystem)
{
    const NetworkInit network;
    const auto opened = OpenTcp();
    ASSERT_TRUE(opened.socket.IsOpen());

    // El socket no está conectado: si llamaran a recv o a send, darían un error.
    const auto received = Client::Network::SocketApi::Receive(opened.socket.Get(), {});
    const auto sent = Client::Network::SocketApi::Send(opened.socket.Get(), {});

    EXPECT_EQ(received.status, Status::Done);
    EXPECT_EQ(received.bytes, 0u);
    EXPECT_EQ(sent.status, Status::Done);
    EXPECT_EQ(sent.bytes, 0u);
}
} // namespace
