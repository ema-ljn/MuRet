#include "DrainQueue.h"
#include "LabSocket.h"
#include "LoopbackPair.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Labs::LoopbackPair, las actividades 5 y 6 de la guía: un connect que no bloquea, "probá más
// tarde", 5 bytes que llegan como 5, un cierre que se lee como 0, y un puerto cerrado que rechaza.
// Todo con sockets de verdad en 127.0.0.1, en un solo hilo: el test pregunta y el sistema avanza
// en el fondo.
namespace
{
using Labs::IoResult;
using Labs::IoStatus;
using Labs::LabSocket;
using Labs::LoopbackPair;
using Labs::Tests::Sequence;
using Labs::Tests::WaitUntil;

// Un buffer de lectura cualquiera, más grande que lo que manda cada test.
constexpr std::size_t ReadBufferSize = 64;
// De a cuánto se manda para llenar el buffer del sistema, y hasta cuánto: el de loopback mide unos
// pocos MiB, así que con 256 MiB tiene que llenarse mucho antes.
constexpr std::size_t FillChunkSize = 64 * 1024;
constexpr std::size_t FillLimit = 256 * 1024 * 1024;
// Lo que se manda después de que el otro lado cerró: alcanza con 1.024 bytes por vez.
constexpr std::size_t AfterCloseChunkSize = 1024;

class LoopbackPairTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_network.Ok());
        m_pair = LoopbackPair::Open();
        ASSERT_TRUE(m_pair.has_value());
    }

    // Hasta que el listener acepte al cliente.
    void AcceptTheClient()
    {
        ASSERT_TRUE(WaitUntil([this] { return m_pair->Accept(); })) << "el listener no aceptó al cliente";
    }

    // Lee de `socket` hasta tener `count` bytes, o hasta el tope de tiempo real, de a `chunk` como
    // mucho.
    static std::vector<std::byte> ReadExactly(LabSocket& socket, std::size_t count, std::size_t chunk = ReadBufferSize)
    {
        std::vector<std::byte> received;
        std::array<std::byte, ReadBufferSize> buffer{};
        const std::span<std::byte> window = std::span<std::byte>(buffer).first(std::min(chunk, buffer.size()));
        WaitUntil(
            [&]
            {
                const IoResult result = socket.Receive(window);
                if (result.status == IoStatus::Done)
                {
                    received.insert(received.end(), window.begin(),
                                    window.begin() + static_cast<std::ptrdiff_t>(result.bytes));
                }
                return received.size() >= count || result.status == IoStatus::PeerClosed ||
                       result.status == IoStatus::Failed;
            });
        return received;
    }

    Labs::NetworkInit m_network;
    std::optional<LoopbackPair> m_pair;
};

// El connect no bloqueante vuelve enseguida: en curso, o ya conectado si el sistema terminó el
// handshake adentro de la llamada, que en loopback puede pasar.
TEST_F(LoopbackPairTest, TheConnectReturnsAtOnce)
{
    const IoStatus started = m_pair->ConnectStarted().status;

    EXPECT_TRUE(started == IoStatus::InProgress || started == IoStatus::Done)
        << ::testing::PrintToString(started) << ", error " << m_pair->ConnectStarted().systemError;
    EXPECT_NE(m_pair->Port(), 0);
}

// El accept solo da conexiones terminadas, y el cliente termina su lado antes que el servidor: una
// vez aceptado, el cliente ya es escribible y SO_ERROR da 0.
TEST_F(LoopbackPairTest, AfterTheAcceptTheClientIsConnected)
{
    AcceptTheClient();

    EXPECT_EQ(m_pair->Client().CheckConnect().status, IoStatus::Done);
}

// Sin nada que leer, un recv no bloqueante no espera: "probá más tarde", en los dos lados.
TEST_F(LoopbackPairTest, AReceiveWithNothingIsTryLater)
{
    AcceptTheClient();
    std::array<std::byte, ReadBufferSize> buffer{};

    EXPECT_EQ(m_pair->Client().Receive(buffer).status, IoStatus::TryLater);
    EXPECT_EQ(m_pair->Server().Receive(buffer).status, IoStatus::TryLater);
}

// Un buffer vacío da 0 sin llamar a recv. Si lo llamara, con datos esperando recv daría 0, que se
// lee como un cierre (sin datos, en Linux da EAGAIN). Por eso el test deja 4 bytes esperando: lee
// 1 de los 5 para saber que llegaron, y después los otros 4 siguen ahí.
TEST_F(LoopbackPairTest, AnEmptyReceiveIsNotAClose)
{
    AcceptTheClient();
    const std::vector<std::byte> sent = Sequence(5, 1);
    ASSERT_EQ(m_pair->Server().Send(sent).bytes, 5u);
    ASSERT_EQ(ReadExactly(m_pair->Client(), 1, 1), Sequence(1, 1));

    const IoResult result = m_pair->Client().Receive({});

    EXPECT_EQ(result.status, IoStatus::Done);
    EXPECT_EQ(result.bytes, 0u);
    EXPECT_EQ(ReadExactly(m_pair->Client(), 4), Sequence(4, 2));
}

// 5 bytes de un lado llegan como 5 del otro, y en el orden en que salieron. TCP no garantiza que
// lleguen en una sola lectura: por eso se lee hasta tener 5.
TEST_F(LoopbackPairTest, FiveBytesArriveAsFive)
{
    AcceptTheClient();
    const std::vector<std::byte> sent = Sequence(5, 1);

    const IoResult result = m_pair->Client().Send(sent);
    ASSERT_EQ(result.status, IoStatus::Done);
    ASSERT_EQ(result.bytes, 5u);

    EXPECT_EQ(ReadExactly(m_pair->Server(), 5), sent);
}

// Cerrar un lado manda un FIN, y el otro lee 0: PeerClosed sin error del sistema.
TEST_F(LoopbackPairTest, ClosingOneSideMakesTheOtherReadZero)
{
    AcceptTheClient();
    m_pair->Server().Close();

    std::array<std::byte, ReadBufferSize> buffer{};
    IoResult last;
    EXPECT_TRUE(WaitUntil(
        [&]
        {
            last = m_pair->Client().Receive(buffer);
            return last.status != IoStatus::TryLater;
        }));
    EXPECT_EQ(last.status, IoStatus::PeerClosed);
    EXPECT_EQ(last.systemError, 0);
}

// Un puerto cerrado: un listener que se abre, dice su puerto y se cierra. El connect termina con
// ECONNREFUSED o WSAECONNREFUSED, desde la misma llamada (puede pasar en Linux) o en SO_ERROR. En
// Windows tarda unos 2 s, por los reintentos.
TEST_F(LoopbackPairTest, AClosedPortIsRefused)
{
    std::uint16_t port = 0;
    {
        LabSocket listener = LabSocket::OpenTcp();
        ASSERT_TRUE(listener.BindLoopback(0));
        ASSERT_TRUE(listener.Listen());
        port = listener.LocalPort();
    }
    LabSocket client = LabSocket::OpenTcp();
    ASSERT_TRUE(client.SetNonBlocking());

    IoResult result = client.ConnectLoopback(port);
    EXPECT_TRUE(WaitUntil(
        [&]
        {
            if (result.status == IoStatus::InProgress)
            {
                result = client.CheckConnect();
            }
            return result.status != IoStatus::InProgress;
        }));
    EXPECT_EQ(result.status, IoStatus::Refused) << "error " << result.systemError;
}

// Mandar a uno que cerró: el primer send suele entrar (el sistema todavía no sabe), el otro lado
// contesta con un RST, y los siguientes dan EPIPE o ECONNRESET, que son PeerClosed. En Linux, sin
// MSG_NOSIGNAL, el primero de esos mataría el proceso con SIGPIPE y el test no terminaría.
TEST_F(LoopbackPairTest, SendingToAClosedPeerDoesNotKillTheProcess)
{
    AcceptTheClient();
    m_pair->Server().Close();
    std::array<std::byte, ReadBufferSize> buffer{};
    ASSERT_TRUE(WaitUntil([&] { return m_pair->Client().Receive(buffer).status == IoStatus::PeerClosed; }));

    const std::vector<std::byte> chunk = Sequence(AfterCloseChunkSize);
    std::vector<IoStatus> statuses;
    EXPECT_TRUE(WaitUntil(
        [&]
        {
            statuses.push_back(m_pair->Client().Send(chunk).status);
            return statuses.back() != IoStatus::Done && statuses.back() != IoStatus::TryLater;
        }));
    ASSERT_FALSE(statuses.empty());
    EXPECT_EQ(statuses.back(), IoStatus::PeerClosed);
}

// Si el otro lado no lee, el buffer del sistema se llena, y un send toma menos de lo pedido o
// nada ("probá más tarde"): eso es lo que la cola guarda para después.
TEST_F(LoopbackPairTest, ASendThatDoesNotFitTakesLessThanAsked)
{
    AcceptTheClient();
    const std::vector<std::byte> chunk = Sequence(FillChunkSize);

    std::size_t total = 0;
    IoResult result;
    while (total < FillLimit)
    {
        result = m_pair->Client().Send(chunk);
        if (result.status != IoStatus::Done || result.bytes < chunk.size())
        {
            break;
        }
        total += result.bytes;
    }

    EXPECT_TRUE(result.status == IoStatus::TryLater || (result.status == IoStatus::Done && result.bytes < chunk.size()))
        << ::testing::PrintToString(result.status) << " con " << result.bytes << " bytes, después de " << total;
}

// La cola manda por un socket de verdad: LabSocket::Send cumple con lo que pide Flush.
TEST_F(LoopbackPairTest, ADrainQueueFlushesIntoTheClient)
{
    AcceptTheClient();
    Labs::DrainQueue queue;
    const std::vector<std::byte> sent = Sequence(10);
    ASSERT_TRUE(queue.Push(sent));

    EXPECT_EQ(queue.Flush([this](std::span<const std::byte> bytes) { return m_pair->Client().Send(bytes); }),
              IoStatus::Done);
    EXPECT_EQ(queue.Pending(), 0u);
    EXPECT_EQ(ReadExactly(m_pair->Server(), sent.size()), sent);
}
} // namespace
