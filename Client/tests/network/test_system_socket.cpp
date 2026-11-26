#include "Network/SystemSocket.h"

#include "Network/ISocket.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"
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

// Real sockets against a LoopbackServer on 127.0.0.1, all on one thread: what the system does in the background (the
// connect, the accept, bytes or the close arriving) is awaited with RetryUntil, up to 5 real seconds.
namespace
{
using Client::Network::ConnectProgress;
using Client::Network::SocketError;
using Client::Network::SystemResolver;
using Client::Network::SystemSocket;
using Client::Network::SocketApi::NetworkInit;
namespace SocketApi = Client::Network::SocketApi;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::FakeResolver;
using Client::Tests::LoopbackServer;
using Client::Tests::RetryUntil;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

constexpr const char* Loopback = "127.0.0.1";
// Larger than anything a test sends: a Receive brings whatever arrived.
constexpr std::size_t ReadSize = 64;

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

    // In this order: the sockets die before the NetworkInit.
    NetworkInit m_network;
    SystemResolver m_resolver;
    LoopbackServer m_server;
    SystemSocket m_socket{m_resolver};
};

TEST_F(SystemSocketTest, ConnectsToALoopbackListener)
{
    ASSERT_TRUE(ConnectTo(m_socket));

    EXPECT_TRUE(m_socket.IsOpen());
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

// With bytes waiting, a 0-byte recv gives 0, which is what a close gives: Receive does not call it.
TEST_F(SystemSocketTest, AnEmptyReceiveIsNotAClose)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.Write(Bytes({0xC1, 0x04, 0x00, 0x01}));
    // They left in a single send.
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

// A RST, like 0x4e8380's kick: recv gives ECONNRESET, which is also the other side closing.
TEST_F(SystemSocketTest, AResetIsClosed)
{
    ASSERT_TRUE(ConnectTo(m_socket));
    m_server.ResetClient();

    EXPECT_EQ(ReceiveUntilAnError(m_socket), SocketError::Closed);
}

// The port of a socket bound to 127.0.0.1 without listen, alive the whole test: nobody else can take it, and the
// connect cannot loop back to itself. The SYN finds no listener and a RST comes back: on Linux the connect gives
// EINPROGRESS and the refusal arrives on the first PollConnect; on Windows it takes about 2 s, from the retries.
// SO_ERROR is cleared when read, and the next PollConnect would give Connected if the result were not kept.
TEST_F(SystemSocketTest, AClosedPortIsRefused)
{
    const SocketApi::Opened bound = SocketApi::BindOnLoopback();
    ASSERT_TRUE(bound.socket.IsOpen()) << "bind: " << bound.result.systemError;
    const std::uint16_t closedPort = SocketApi::LocalPort(bound.socket.Get());
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

// Depends on the DNS: left to the manual test.
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

// After the FIN the first send leaves and the other side answers with a RST; the next gives EPIPE, and on Linux,
// without MSG_NOSIGNAL, SIGPIPE would kill the test process. Windows has no signal: it gives WSAECONNRESET or
// WSAECONNABORTED when the RST, processed in the background, arrives. So it sends 1,024 bytes at a time until the
// first error, within RetryUntil's cap: what a send gives after the first error is not part of the contract.
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
