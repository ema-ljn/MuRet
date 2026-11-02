#include "Network/SystemSocket.h"

#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/FakeResolver.h"
#include "support/LoopbackServer.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// Client::Network::SystemSocket, con sockets de verdad contra un LoopbackServer en 127.0.0.1 (el
// plan de la CL4, CL4.1, paso 3). Todo en un hilo: lo que el sistema hace en el fondo (el connect,
// el accept, que lleguen los bytes o el cierre) se espera con RetryUntil, hasta 5 s reales.
namespace
{
using Client::Network::ConnectProgress;
using Client::Network::SocketError;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::FakeResolver;
using Client::Tests::LoopbackServer;
using Client::Tests::RetryUntil;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

constexpr const char* Loopback = "127.0.0.1";
// Más grande que todo lo que manda un test: un Receive trae lo que haya llegado.
constexpr std::size_t ReadSize = 64;

// Lo que dio Receive mientras no hubo un error, hasta juntar `count` bytes o hasta el tope, de a
// `readSize` bytes por llamada.
ByteVector ReceiveAtLeast(SystemSocket& socket, std::size_t count, std::size_t readSize = ReadSize)
{
    ByteVector received;
    const bool arrived = RetryUntil(
        [&]
        {
            std::array<std::byte, ReadSize> buffer{};
            const auto taken = socket.Receive(std::span(buffer).first(readSize));
            if (taken.HasValue())
            {
                received.insert(received.end(), buffer.begin(),
                                buffer.begin() + static_cast<std::ptrdiff_t>(taken.Value()));
            }
            return !taken.HasValue() || received.size() >= count;
        });
    EXPECT_TRUE(arrived) << "llegaron " << received.size() << " de " << count << " bytes";
    return received;
}

ByteVector ReadAtLeast(LoopbackServer& server, std::size_t count)
{
    ByteVector received;
    const bool arrived = RetryUntil(
        [&]
        {
            const ByteVector read = server.Read();
            received.insert(received.end(), read.begin(), read.end());
            return received.size() >= count;
        });
    EXPECT_TRUE(arrived) << "llegaron " << received.size() << " de " << count << " bytes";
    return received;
}

// El primer error de Receive, o nullopt si no hubo ninguno antes del tope.
std::optional<SocketError> ReceiveUntilAnError(SystemSocket& socket)
{
    std::optional<SocketError> error;
    const bool failed = RetryUntil(
        [&]
        {
            std::array<std::byte, ReadSize> buffer{};
            error = ErrorOf(socket.Receive(buffer));
            return error.has_value();
        });
    EXPECT_TRUE(failed) << "Receive nunca dio un error";
    return error;
}

class SystemSocketTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_network.IsReady()) << "WSAStartup: " << m_network.SystemError();
        ASSERT_NE(m_server.Port(), 0);
    }

    // StartConnect al servidor, y PollConnect y Accept en el mismo lazo hasta que los dos digan
    // que sí.
    ::testing::AssertionResult ConnectTo(SystemSocket& socket)
    {
        const auto started = socket.StartConnect(Loopback, m_server.Port());
        if (!started.HasValue())
        {
            return ::testing::AssertionFailure() << "StartConnect dio un error";
        }
        bool connected = started.Value() == ConnectProgress::Connected;
        bool accepted = false;
        bool failed = false;
        const bool done = RetryUntil(
            [&]
            {
                if (!connected)
                {
                    const auto polled = socket.PollConnect();
                    failed = !polled.HasValue();
                    connected = !failed && polled.Value() == ConnectProgress::Connected;
                }
                accepted = accepted || m_server.Accept();
                return failed || (connected && accepted);
            });
        if (failed || !done)
        {
            return ::testing::AssertionFailure() << "connected " << connected << ", accepted " << accepted;
        }
        return ::testing::AssertionSuccess();
    }

    // En este orden: los sockets mueren antes que el NetworkInit.
    NetworkInit m_network;
    SystemResolver m_resolver;
    LoopbackServer m_server;
    SystemSocket m_socket{m_resolver};
};

TEST_F(SystemSocketTest, ConnectsToALoopbackListener)
{
    ASSERT_TRUE(ConnectTo(m_socket));

    EXPECT_TRUE(m_socket.IsOpen());
    // El resultado final queda guardado.
    EXPECT_EQ(m_socket.PollConnect().Value(), ConnectProgress::Connected);
    EXPECT_EQ(m_socket.PollConnect().Value(), ConnectProgress::Connected);
}

TEST_F(SystemSocketTest, ReceiveWithNothingIsZero)
{
    ASSERT_TRUE(ConnectTo(m_socket));

    std::array<std::byte, ReadSize> buffer{};
    const auto taken = m_socket.Receive(buffer);
    ASSERT_EQ(ErrorOf(taken), std::nullopt);
    EXPECT_EQ(taken.Value(), 0u);
}

// Con bytes esperando, un recv de 0 bytes da 0, que es lo que da un cierre: Receive no lo llama.
TEST_F(SystemSocketTest, AnEmptyReceiveIsNotAClose)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.Write(Bytes({0xC1, 0x04, 0x00, 0x01}));
    // Cuando llega el primero, los otros tres ya esperan en el socket: salieron en un solo send.
    ASSERT_EQ(ReceiveAtLeast(m_socket, 1, 1).size(), 1u);

    const auto empty = m_socket.Receive(std::span<std::byte>());
    ASSERT_EQ(ErrorOf(empty), std::nullopt);
    EXPECT_EQ(empty.Value(), 0u);
    EXPECT_TRUE(SameBytes(ReceiveAtLeast(m_socket, 3), Bytes({0x04, 0x00, 0x01})));
}

TEST_F(SystemSocketTest, SendsAndReceivesBytes)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    const ByteVector request = Bytes({0xC1, 0x04, 0xF4, 0x02});
    const ByteVector hello = Bytes({0xC1, 0x04, 0x00, 0x01});

    EXPECT_EQ(m_socket.Send(request).Value(), request.size());
    EXPECT_TRUE(SameBytes(ReadAtLeast(m_server, request.size()), request));
    m_server.Write(hello);
    EXPECT_TRUE(SameBytes(ReceiveAtLeast(m_socket, hello.size()), hello));
}

TEST_F(SystemSocketTest, APeerThatClosesIsClosed)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.CloseClient();

    EXPECT_EQ(ReceiveUntilAnError(m_socket), SocketError::Closed);
}

// Un RST, como el kick de 0x4e8380: recv da ECONNRESET, que también es que el otro lado cerró.
TEST_F(SystemSocketTest, AResetIsClosed)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.ResetClient();

    EXPECT_EQ(ReceiveUntilAnError(m_socket), SocketError::Closed);
}

// Un puerto donde nadie escucha: el de un listener que ya cerró. En Linux el rechazo llega
// enseguida y en Windows tarda unos 2 s, por los reintentos. SO_ERROR se borra al leerlo, y el
// siguiente PollConnect daría Connected si el resultado no quedara guardado.
TEST_F(SystemSocketTest, AClosedPortIsRefused)
{
    std::uint16_t closedPort = 0;
    {
        const LoopbackServer closed;
        closedPort = closed.Port();
    }
    ASSERT_NE(closedPort, 0);

    auto result = m_socket.StartConnect(Loopback, closedPort);
    const bool finished = RetryUntil(
        [&]
        {
            if (result.HasValue() && result.Value() == ConnectProgress::InProgress)
            {
                result = m_socket.PollConnect();
                return !result.HasValue() || result.Value() != ConnectProgress::InProgress;
            }
            return true;
        });
    ASSERT_TRUE(finished) << "el connect siguió en curso";

    EXPECT_EQ(ErrorOf(result), SocketError::Refused);
    EXPECT_EQ(ErrorOf(m_socket.PollConnect()), SocketError::Refused);
}

// El SystemResolver con un nombre que no existe depende del DNS: queda para la prueba a mano.
TEST_F(SystemSocketTest, AnUnknownHostFailsToResolve)
{
    FakeResolver nowhere;
    SystemSocket socket(nowhere);

    EXPECT_EQ(ErrorOf(socket.StartConnect("nombre.invalid", m_server.Port())), SocketError::ResolveFailed);
    EXPECT_FALSE(socket.IsOpen());
    EXPECT_EQ(ErrorOf(socket.PollConnect()), SocketError::ResolveFailed);
    ASSERT_EQ(nowhere.hosts.size(), 1u);
    EXPECT_EQ(nowhere.hosts[0], "nombre.invalid");
}

// Después del FIN, el primer send sale y el otro lado contesta con un RST; el siguiente da EPIPE, y
// en Linux, sin MSG_NOSIGNAL, la señal SIGPIPE mataría el proceso del test. En Windows no hay
// señal: da WSAECONNRESET o WSAECONNABORTED, cuando llega el RST, que se procesa en el fondo. Por
// eso se manda de a 1.024 bytes hasta el primer error, con el tope de RetryUntil, y no diez veces
// seguidas: lo que da un send después del primer error no es parte del contrato.
TEST_F(SystemSocketTest, SendingAfterThePeerClosedDoesNotKillTheProcess)
{
    constexpr std::size_t SendSize = 1024;
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.CloseClient();
    ASSERT_EQ(ReceiveUntilAnError(m_socket), SocketError::Closed);

    const ByteVector bytes(SendSize, std::byte{0x55});
    int sends = 0;
    std::optional<SocketError> error;
    const bool failed = RetryUntil(
        [&]
        {
            ++sends;
            error = ErrorOf(m_socket.Send(bytes));
            return error.has_value();
        });

    ASSERT_TRUE(failed) << "ningún Send dio un error en " << sends;
    EXPECT_GE(sends, 2) << "el primero, antes del RST, tendría que salir";
    EXPECT_EQ(error, SocketError::Closed);
}

TEST_F(SystemSocketTest, AMovedSocketIsClosed)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    SystemSocket moved(std::move(m_socket));
    const ByteVector request = Bytes({0xC1, 0x04, 0xF4, 0x02});

    EXPECT_FALSE(m_socket.IsOpen());
    EXPECT_EQ(ErrorOf(m_socket.PollConnect()), SocketError::Closed);
    EXPECT_EQ(ErrorOf(m_socket.Send(request)), SocketError::Closed);
    EXPECT_TRUE(moved.IsOpen());
    EXPECT_EQ(moved.PollConnect().Value(), ConnectProgress::Connected);
    EXPECT_EQ(moved.Send(request).Value(), request.size());
    EXPECT_TRUE(SameBytes(ReadAtLeast(m_server, request.size()), request));
}

// El que recibe cierra el socket que tenía: el servidor ve el cierre.
TEST_F(SystemSocketTest, AMoveAssignmentClosesTheOldOne)
{
    SystemSocket other(m_resolver);
    ASSERT_TRUE(ConnectTo(other));

    other = std::move(m_socket);

    EXPECT_FALSE(other.IsOpen());
    EXPECT_EQ(ErrorOf(other.PollConnect()), SocketError::Closed);
    EXPECT_TRUE(RetryUntil(
        [&]
        {
            static_cast<void>(m_server.Read());
            return m_server.ClientClosed();
        }));
}

TEST_F(SystemSocketTest, CanConnectAgainAfterClose)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_socket.Close();
    const ByteVector request = Bytes({0xC1, 0x04, 0xF4, 0x02});

    EXPECT_FALSE(m_socket.IsOpen());
    EXPECT_EQ(ErrorOf(m_socket.PollConnect()), SocketError::Closed);
    EXPECT_EQ(ErrorOf(m_socket.Send(request)), SocketError::Closed);
    std::array<std::byte, ReadSize> buffer{};
    EXPECT_EQ(ErrorOf(m_socket.Receive(buffer)), SocketError::Closed);

    ASSERT_TRUE(ConnectTo(m_socket));
    EXPECT_EQ(m_socket.Send(request).Value(), request.size());
    EXPECT_TRUE(SameBytes(ReadAtLeast(m_server, request.size()), request));
}
} // namespace
