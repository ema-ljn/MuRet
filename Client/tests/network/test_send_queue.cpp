#include "Network/SendQueue.h"

#include "Core/Expected.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"
#include "support/Bytes.h"
#include "support/FakeSocket.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Client::Network::SendQueue: el envío que no entró, con un cursor (el plan de la CL4, CL4.1,
// paso 5, y Q-C11), sobre un FakeSocket que toma lo que dice el guion.
namespace
{
using Client::Core::Expected;
using Client::Network::ConnectProgress;
using Client::Network::MaxPendingSend;
using Client::Network::SendQueue;
using Client::Network::SendQueueError;
using Client::Network::SocketError;
using Client::Tests::Bytes;
using Client::Tests::FakeSocket;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

constexpr std::uint16_t AnyPort = 44405;

// `count` bytes que no se repiten cada 256, desde `first`: un byte corrido o repetido se ve.
ByteVector Sequence(std::size_t first, std::size_t count)
{
    ByteVector bytes;
    bytes.reserve(count);
    for (std::size_t index = first; index < first + count; ++index)
    {
        bytes.push_back(static_cast<std::byte>(index * 7 + index / 256));
    }
    return bytes;
}

// Un ISocket roto: el primer Send dice haber tomado 5 bytes más de los que se le dieron, y los
// siguientes, 0. No lee lo que se le da.
class ClaimsMoreSocket final : public Client::Network::ISocket
{
public:
    static constexpr std::size_t Extra = 5;

    int sends = 0;

    Expected<ConnectProgress, SocketError> StartConnect(std::string_view, std::uint16_t) override
    {
        return ConnectProgress::Connected;
    }
    Expected<ConnectProgress, SocketError> PollConnect() override
    {
        return ConnectProgress::Connected;
    }
    Expected<std::size_t, SocketError> Receive(std::span<std::byte>) override
    {
        return std::size_t{0};
    }
    Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) override
    {
        ++sends;
        if (sends == 1)
        {
            return bytes.size() + Extra;
        }
        return std::size_t{0};
    }
    void Close() override {}
    [[nodiscard]] bool IsOpen() const override
    {
        return true;
    }
    [[nodiscard]] int LastSystemError() const override
    {
        return 0;
    }
};

class SendQueueTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(m_socket.StartConnect("127.0.0.1", AnyPort).Value(), ConnectProgress::Connected);
    }

    FakeSocket m_socket;
    SendQueue m_queue;
};

TEST_F(SendQueueTest, SendsEverythingWhenTheSocketTakesIt)
{
    const ByteVector bytes = Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

    EXPECT_EQ(m_queue.Push(bytes), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), bytes.size());
    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);

    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_TRUE(SameBytes(m_socket.sent, bytes));
}

// Q-C11: el drenaje de 0x4e87b0, con este guion, manda 00 01 02 03 en el primer FD_WRITE y
// 00 01 02 03 04 05 en el segundo: repite 00..03 y pierde 06..09 (lo va a fijar el jefe de la
// CL4.0, paso 5). Con el cursor, 00..09 sale una vez y en orden.
TEST_F(SendQueueTest, PartialSendsKeepTheOrder)
{
    const ByteVector bytes = Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    m_socket.sendLimits = {std::size_t{2}, std::size_t{2}, std::nullopt};
    ASSERT_EQ(m_queue.Push(bytes), std::nullopt);

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), 6u);
    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0, 1, 2, 3})));

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_TRUE(SameBytes(m_socket.sent, bytes));
}

// El original encola si `pendiente + nuevo < 0x2001`, y si no, cierra.
TEST_F(SendQueueTest, FillsUpToTheOriginalBuffer)
{
    EXPECT_EQ(m_queue.Push(Sequence(0, MaxPendingSend)), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), MaxPendingSend);

    EXPECT_EQ(m_queue.Push(Bytes({0xFF})), SendQueueError::Full);
    EXPECT_EQ(m_queue.Pending(), MaxPendingSend);

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_TRUE(SameBytes(m_socket.sent, Sequence(0, MaxPendingSend)));
}

TEST_F(SendQueueTest, MoreThanTheBufferInOnePushIsFull)
{
    EXPECT_EQ(m_queue.Push(Sequence(0, MaxPendingSend + 1)), SendQueueError::Full);
    EXPECT_EQ(m_queue.Pending(), 0u);
}

// Lo que ya salió no ocupa lugar: el cursor no hace crecer el buffer.
TEST_F(SendQueueTest, ReusesTheBufferAfterAPartialSend)
{
    constexpr std::size_t Half = MaxPendingSend / 2;
    ASSERT_EQ(m_queue.Push(Sequence(0, MaxPendingSend)), std::nullopt);
    m_socket.sendLimits = {Half, std::nullopt};
    ASSERT_EQ(m_queue.Flush(m_socket), std::nullopt);
    ASSERT_EQ(m_queue.Pending(), Half);

    EXPECT_EQ(m_queue.Push(Sequence(MaxPendingSend, Half)), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), MaxPendingSend);
    EXPECT_EQ(m_queue.Push(Bytes({0xFF})), SendQueueError::Full);

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_TRUE(SameBytes(m_socket.sent, Sequence(0, MaxPendingSend + Half)));
}

// Lo nuevo va detrás de lo pendiente: el envío en línea del original (0x4b1664) llama a send sin
// mirar lo encolado, y un paquete nuevo sale antes que el resto del anterior.
TEST_F(SendQueueTest, NewBytesWaitBehindThePending)
{
    ASSERT_EQ(m_queue.Push(Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9})), std::nullopt);
    m_socket.sendLimits = {std::size_t{6}, std::nullopt};
    ASSERT_EQ(m_queue.Flush(m_socket), std::nullopt);
    ASSERT_EQ(m_queue.Pending(), 4u);

    EXPECT_EQ(m_queue.Push(Bytes({0x0A, 0x0B})), std::nullopt);
    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);

    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0x0A, 0x0B})));
}

TEST_F(SendQueueTest, NoSocketErrorBeforeTheFirst)
{
    EXPECT_EQ(m_queue.LastSocketError(), std::nullopt);

    ASSERT_EQ(m_queue.Push(Bytes({0, 1, 2})), std::nullopt);
    ASSERT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_queue.LastSocketError(), std::nullopt);
}

TEST_F(SendQueueTest, ASocketErrorIsReported)
{
    const ByteVector bytes = Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    ASSERT_EQ(m_queue.Push(bytes), std::nullopt);
    m_socket.sendError = SocketError::Closed;

    EXPECT_EQ(m_queue.Flush(m_socket), SendQueueError::Socket);
    EXPECT_EQ(m_queue.LastSocketError(), SocketError::Closed);
    EXPECT_EQ(m_queue.Pending(), bytes.size());

    // Lo pendiente quedó entero: con el socket de vuelta, sale todo.
    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_TRUE(SameBytes(m_socket.sent, bytes));
}

// Un error en la mitad de un Flush: lo que ya salió no vuelve, lo demás queda. Si el cursor
// volviera a donde estaba al empezar el Flush, el segundo mandaría 00 01 otra vez.
TEST_F(SendQueueTest, AnErrorMidFlushKeepsWhatDidNotLeave)
{
    ASSERT_EQ(m_queue.Push(Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9})), std::nullopt);
    m_socket.sendLimits = {std::size_t{2}, SocketError::Closed};

    EXPECT_EQ(m_queue.Flush(m_socket), SendQueueError::Socket);
    EXPECT_EQ(m_queue.LastSocketError(), SocketError::Closed);
    EXPECT_EQ(m_queue.Pending(), 8u);
    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0, 1})));

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9})));
}

// Un socket roto que dice haber tomado más de lo que se le dio: la cola lo toma como que tomó
// todo, y el cursor no pasa del final (sin el tope, Pending daría la vuelta y Flush seguiría
// pidiendo).
TEST_F(SendQueueTest, ASocketThatClaimsMoreDoesNotRunOff)
{
    ClaimsMoreSocket socket;
    ASSERT_EQ(m_queue.Push(Bytes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9})), std::nullopt);

    EXPECT_EQ(m_queue.Flush(socket), std::nullopt);

    EXPECT_EQ(socket.sends, 1);
    EXPECT_EQ(m_queue.Pending(), 0u);
    // El buffer quedó libre entero.
    EXPECT_EQ(m_queue.Push(Sequence(0, MaxPendingSend)), std::nullopt);
}

TEST_F(SendQueueTest, FlushingNothingDoesNotCallTheSocket)
{
    m_socket.sendError = SocketError::Failed;

    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_EQ(m_socket.sendError, SocketError::Failed);
}

TEST_F(SendQueueTest, ClearDropsThePending)
{
    ASSERT_EQ(m_queue.Push(Bytes({0, 1, 2})), std::nullopt);

    m_queue.Clear();

    EXPECT_EQ(m_queue.Pending(), 0u);
    EXPECT_EQ(m_queue.Flush(m_socket), std::nullopt);
    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(m_queue.Push(Sequence(0, MaxPendingSend)), std::nullopt);
}
} // namespace
