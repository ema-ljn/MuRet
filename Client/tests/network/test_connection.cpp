#include "Network/Connection.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Delivery.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/SendQueue.h"
#include "Network/SocketError.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Client::Network::Connection: el camino de un paquete en claro por una conexión (el plan de la
// CL4, CL4.3, paso 2), sobre un FakeSocket que sigue un guion y un FakeClock que no se mueve solo.
namespace
{
using namespace std::chrono_literals;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::Closed;
using Client::Network::CloseReason;
using Client::Network::Connection;
using Client::Network::ConnectTimeout;
using Client::Network::Delivery;
using Client::Network::Event;
using Client::Network::FoxAntiObfuscation;
using Client::Network::IdentityObfuscation;
using Client::Network::LinkState;
using Client::Network::MaxPacketSize;
using Client::Network::MaxReadsPerPoll;
using Client::Network::Opened;
using Client::Network::Packet;
using Client::Network::SlowConnectStart;
using Client::Network::SocketError;
using Client::Protocol::Connection::WriteServerSelect;
using Client::Tests::Bytes;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::PacketOf;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

// El ConnectServer y el GameServer de los tests: cualquier dirección, el FakeSocket no conecta.
constexpr std::string_view ConnectHost = "127.0.0.1";
constexpr std::uint16_t ConnectPort = 44405;
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// Un error del sistema para el log de un SocketFailed: el número no importa, solo que se vea.
constexpr int SomeSystemError = 12345;

// El saludo del ConnectServer y el pedido de la lista (04 § El flujo de conexión).
ByteVector Hello()
{
    return Bytes({0xC1, 0x04, 0x00, 0x01});
}

ByteVector ListRequest()
{
    return Bytes({0xC1, 0x04, 0xF4, 0x02});
}

// Un C2 de `size` bytes con un opcode cualquiera y el resto en 0.
ByteVector C2Of(std::size_t size)
{
    ByteVector bytes(size);
    const ByteVector header = Bytes({0xC2, static_cast<int>(size >> 8), static_cast<int>(size & 0xFF), 0x99});
    std::copy(header.begin(), header.end(), bytes.begin());
    return bytes;
}

// Pega varios paquetes en una sola lectura, como llegan del cable.
ByteVector Join(std::initializer_list<ByteVector> parts)
{
    ByteVector joined;
    for (const ByteVector& part : parts)
    {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

std::string HexOf(std::span<const std::byte> bytes)
{
    std::string text;
    for (const std::byte value : bytes)
    {
        text += std::format("{}{:02X}", text.empty() ? "" : " ", std::to_integer<int>(value));
    }
    return text;
}

// Un lote de Poll en palabras: "Opened", los bytes de cada paquete ("C1 04 00 01") y
// "Closed(PeerClosed)". Así un test compara el lote entero, en orden, y si falla, el mensaje dice
// qué llegó. Son bytes escritos en el test, no del cable.
Lines Describe(const std::vector<Event>& events)
{
    Lines lines;
    for (const Event& event : events)
    {
        if (std::holds_alternative<Opened>(event))
        {
            lines.emplace_back("Opened");
        }
        else if (const Closed* closed = std::get_if<Closed>(&event))
        {
            lines.push_back("Closed(" + ::testing::PrintToString(closed->reason) + ")");
        }
        else
        {
            lines.push_back(HexOf(std::get<Packet>(event).Bytes()));
        }
    }
    return lines;
}

// Cuántos paquetes trae un lote, para los de miles de bytes.
std::size_t CountPackets(const std::vector<Event>& events)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(events, [](const Event& event) { return std::holds_alternative<Packet>(event); }));
}

class ConnectionTest : public ::testing::Test
{
protected:
    // Open al ConnectServer y el Poll que da el Opened.
    void OpenAndConnect(Connection& connection)
    {
        connection.Open(ConnectHost, ConnectPort);
        ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
    }

    void OpenAndConnect()
    {
        OpenAndConnect(m_connection);
    }

    // Si alguna línea de aviso tiene `part`.
    [[nodiscard]] bool HasWarning(std::string_view part) const
    {
        for (std::size_t index = 0; index < m_sink.lines.size(); ++index)
        {
            if (m_sink.levels[index] == LogLevel::Warning && m_sink.lines[index].find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    FakeSocket m_socket;
    FakeClock m_clock;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
    Connection m_connection{m_socket, IdentityObfuscation::Shared(), m_clock, m_log};
};

TEST_F(ConnectionTest, OpensWhenTheSocketConnects)
{
    m_socket.pollsUntilConnected = 2;

    m_connection.Open(ConnectHost, ConnectPort);
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});

    ASSERT_EQ(m_socket.connects.size(), 1u);
    EXPECT_EQ(m_socket.connects[0].host, ConnectHost);
    EXPECT_EQ(m_socket.connects[0].port, ConnectPort);
    EXPECT_EQ(m_connection.LastCloseReason(), std::nullopt);
}

// En loopback el sistema puede conectar en el mismo connect: el Opened sale igual en un Poll, no
// en Open, así quien la usa lo ve siempre en el mismo lugar.
TEST_F(ConnectionTest, AConnectThatFinishesAtOnceOpensInTheFirstPoll)
{
    m_connection.Open(ConnectHost, ConnectPort);
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
}

TEST_F(ConnectionTest, OpenedComesBeforeWhatArrivesWithIt)
{
    m_socket.pollsUntilConnected = 1;
    m_socket.reads = {Hello()};
    m_connection.Open(ConnectHost, ConnectPort);
    ASSERT_EQ(Describe(m_connection.Poll()), Lines{});

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
}

// Un connect que falla en el StartConnect (el resolver, o un rechazo que el sistema ya sabe) o en
// un PollConnect: el Poll que sigue da Closed con su motivo, una sola vez.
TEST_F(ConnectionTest, AConnectThatFailsCloses)
{
    struct Case
    {
        SocketError error;
        CloseReason reason;
        int pollsUntilDone;
    };
    const Case cases[] = {
        {SocketError::ResolveFailed, CloseReason::ResolveFailed, 0},
        {SocketError::Refused, CloseReason::Refused, 0},
        {SocketError::Refused, CloseReason::Refused, 1},
        {SocketError::Failed, CloseReason::SocketFailed, 1},
    };
    for (const Case& failure : cases)
    {
        SCOPED_TRACE(::testing::PrintToString(failure.reason) + " después de " +
                     std::to_string(failure.pollsUntilDone) + " PollConnect");
        FakeSocket socket;
        socket.connectResult = failure.error;
        socket.pollsUntilConnected = failure.pollsUntilDone;
        Connection connection(socket, IdentityObfuscation::Shared(), m_clock, m_log);

        connection.Open(ConnectHost, ConnectPort);
        if (failure.pollsUntilDone > 0)
        {
            EXPECT_EQ(connection.State(), LinkState::Connecting);
            EXPECT_EQ(Describe(connection.Poll()), Lines{});
        }
        else
        {
            EXPECT_EQ(connection.State(), LinkState::Closed);
        }

        EXPECT_EQ(Describe(connection.Poll()), Lines{"Closed(" + ::testing::PrintToString(failure.reason) + ")"});
        EXPECT_EQ(connection.State(), LinkState::Closed);
        EXPECT_EQ(connection.LastCloseReason(), failure.reason);
        // Después de un Refused el socket del sistema sigue abierto hasta Close (ISocket.h): lo
        // cierra la conexión.
        EXPECT_FALSE(socket.IsOpen());
        EXPECT_EQ(Describe(connection.Poll()), Lines{});
    }
}

TEST_F(ConnectionTest, AConnectThatTakesTooLongTimesOut)
{
    m_socket.pollsUntilConnected = std::numeric_limits<int>::max();
    m_connection.Open(ConnectHost, ConnectPort);

    m_clock.Advance(ConnectTimeout - 1ms);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);

    m_clock.Advance(1ms);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Closed(ConnectTimedOut)"});
    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::ConnectTimedOut);
    EXPECT_FALSE(m_socket.IsOpen());
}

// Primero PollConnect y después el tope: el connect terminó en el fondo durante un frame de 60 s.
TEST_F(ConnectionTest, ALongFrameDoesNotTimeOutAConnectThatFinished)
{
    m_connection.Open(ConnectHost, ConnectPort);

    m_clock.Advance(60s);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
}

// El tope es del connect: lo que tardó el resolver adentro de Open no cuenta.
TEST_F(ConnectionTest, TheConnectTimeoutStartsAfterTheResolve)
{
    m_socket.pollsUntilConnected = std::numeric_limits<int>::max();
    m_socket.onStartConnect = [this] { m_clock.Advance(5s); };
    m_connection.Open(ConnectHost, ConnectPort);

    m_clock.Advance(ConnectTimeout - 1ms);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
}

// Pregunta 5 del plan: el resolver del sistema espera, y lo que tarda va al log si pasa de 100 ms.
TEST_F(ConnectionTest, ASlowResolveIsLogged)
{
    m_socket.onStartConnect = [this] { m_clock.Advance(SlowConnectStart); };
    m_connection.Open(ConnectHost, ConnectPort);
    EXPECT_FALSE(HasWarning("tardó"));

    m_socket.onStartConnect = [this] { m_clock.Advance(SlowConnectStart + 1ms); };
    m_connection.Open(ConnectHost, ConnectPort);
    EXPECT_TRUE(HasWarning("tardó 101 ms"));
}

// ISocket.h: Receive y Send solo con el socket conectado, así un Closed ahí es un cierre del otro
// lado. Mientras conecta, la conexión no lee, no manda ni drena la cola: el guion tiene una lectura
// y un error de envío esperando, y siguen ahí hasta el Opened.
TEST_F(ConnectionTest, NothingIsReadOrSentBeforeTheConnect)
{
    m_socket.pollsUntilConnected = 2;
    m_socket.reads = {Hello()};
    m_socket.sendError = SocketError::Failed;
    m_connection.Open(ConnectHost, ConnectPort);
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
    EXPECT_EQ(m_socket.reads.size(), 1u);
    EXPECT_EQ(m_socket.sendError, SocketError::Failed);

    m_socket.sendError.reset();
    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
    EXPECT_TRUE(SameBytes(m_socket.sent, ListRequest()));
}

TEST_F(ConnectionTest, WhatIsSentWhileConnectingLeavesWhenItOpens)
{
    m_socket.pollsUntilConnected = 1;
    m_connection.Open(ConnectHost, ConnectPort);

    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_TRUE(m_socket.sent.empty());

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_TRUE(SameBytes(m_socket.sent, ListRequest()));
}

// Lo nuevo va detrás de lo que el socket no tomó, nunca directo al ISocket::Send. El original, en
// 0x4b1664, llama a send sin mirar lo encolado, y el C1 04 00 01 saldría antes que el resto del
// F4 02. Con el socket tomando de a 2, lo que sale después de un paquete nuevo es lo pendiente.
TEST_F(ConnectionTest, NewPacketsWaitBehindThePending)
{
    OpenAndConnect();
    m_socket.sendLimits = {std::size_t{2}, std::nullopt};
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    ASSERT_TRUE(SameBytes(m_socket.sent, Bytes({0xC1, 0x04})));

    m_socket.sendLimits = {std::size_t{2}, std::nullopt};
    m_connection.Send(PacketOf(Hello()), Delivery::Clear);
    EXPECT_TRUE(SameBytes(m_socket.sent, ListRequest()));

    m_connection.Send(PacketOf(Bytes({0xC1, 0x03, 0x99})), Delivery::Clear);
    EXPECT_TRUE(SameBytes(m_socket.sent, Join({ListRequest(), Hello(), Bytes({0xC1, 0x03, 0x99})})));
}

TEST_F(ConnectionTest, WhatDidNotLeaveLeavesInTheNextPoll)
{
    OpenAndConnect();
    m_socket.sendLimits = {std::size_t{2}, std::nullopt};
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    ASSERT_TRUE(SameBytes(m_socket.sent, Bytes({0xC1, 0x04})));

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_TRUE(SameBytes(m_socket.sent, ListRequest()));
}

TEST_F(ConnectionTest, CutsWhatArrivesInPieces)
{
    OpenAndConnect();

    m_socket.reads = {Bytes({0xC1, 0x04})};
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    m_socket.reads = {Bytes({0x00, 0x01})};
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"C1 04 00 01"});

    // Y al revés: dos en una lectura.
    m_socket.reads = {Join({Hello(), ListRequest()})};
    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"C1 04 00 01", "C1 04 F4 02"}));
}

// Un frame lee a lo sumo MaxReadsPerPoll veces: lo que queda sale en el siguiente.
TEST_F(ConnectionTest, ReadsAtMostEightTimesPerPoll)
{
    ASSERT_EQ(MaxReadsPerPoll, 8);
    OpenAndConnect();
    for (int read = 0; read < MaxReadsPerPoll + 1; ++read)
    {
        m_socket.reads.push_back(Hello());
    }

    EXPECT_EQ(CountPackets(m_connection.Poll()), 8u);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"C1 04 00 01"});
}

// Cada lectura es de 0x2000 bytes, el buffer de recv del original: nueve paquetes de 0x2000 en una
// sola entrada del guion salen ocho en un Poll (ocho lecturas) y el noveno en el siguiente.
TEST_F(ConnectionTest, ReadsInChunksOfTheOriginalBuffer)
{
    OpenAndConnect();
    ByteVector nine;
    for (int packet = 0; packet < MaxReadsPerPoll + 1; ++packet)
    {
        const ByteVector large = C2Of(MaxPacketSize);
        nine.insert(nine.end(), large.begin(), large.end());
    }
    m_socket.reads = {nine};

    const std::vector<Event> first = m_connection.Poll();
    ASSERT_EQ(CountPackets(first), 8u);
    EXPECT_TRUE(SameBytes(std::get<Packet>(first[0]).Bytes(), C2Of(MaxPacketSize)));
    EXPECT_EQ(CountPackets(m_connection.Poll()), 1u);
}

// 04 § El flujo de conexión: la lista del laboratorio llega al cable como F4 02, y FoxAnti la pasa
// a la F4 06 que lee main.exe, con el código 400.
TEST_F(ConnectionTest, WithFoxAntiTheListArrivesAsF406)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log);
    OpenAndConnect(connection);
    m_socket.reads = {Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C2 00 0B F4 06 00 01 90 01 00 00"});
}

// Y la elección del 400 vuelve al código del laboratorio en el BeforeSend.
TEST_F(ConnectionTest, WithFoxAntiTheChoiceLeavesAsF40300)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log);
    OpenAndConnect(connection);

    Client::Protocol::Outgoing select = WriteServerSelect(400);
    connection.Send(std::move(select.packet), select.delivery);

    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00})));
}

// BeforeDispatch también corre: FoxAnti le mete a un C1 F3 11 el byte que main.exe espera en
// pkt[5].
TEST_F(ConnectionTest, WithFoxAntiTheSkillListGetsItsByteBeforeTheDispatch)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log);
    OpenAndConnect(connection);
    m_socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 06 F3 11 07 00"});
}

// El GameServer del laboratorio manda cada byte con el 0xAA (03 § La capa de FoxAnti): el corte lo
// saca con la máscara de la ofuscación de la conexión, también después de volver a abrir.
TEST_F(ConnectionTest, WithFoxAntiWhatArrivesFromTheGameServerIsUnmasked)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log);
    const ByteVector maskedHello = Bytes({0xC1 ^ 0xAA, 0x04 ^ 0xAA, 0x00 ^ 0xAA, 0x01 ^ 0xAA});
    OpenAndConnect(connection);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});

    connection.Open(GameHost, GamePort);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(connection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
}

// Un InvalidPacket de la ofuscación no corta: el largo lo dio la cabecera, así que el siguiente se
// sabe dónde empieza. Esta lista dice dos entradas y trae una.
TEST_F(ConnectionTest, AListThatDoesNotTranslateIsDropped)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log);
    OpenAndConnect(connection);
    m_socket.reads = {Join({Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x02, 0x00, 0x00, 0x00, 0xCC}), Hello()})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(connection.State(), LinkState::Open);
    EXPECT_TRUE(HasWarning("C2 F4"));
}

// Un error del corte sí: después de una cabecera mala no se sabe dónde empieza el próximo paquete
// (Q-C38). Lo que se cortó antes sale, y el Closed va último.
TEST_F(ConnectionTest, ABadHeaderCloses)
{
    OpenAndConnect();
    m_socket.reads = {Join({Hello(), Bytes({0xFF, 0x00})})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"C1 04 00 01", "Closed(BadStream)"}));
    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::BadStream);
    EXPECT_EQ(m_socket.closes, 1);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
}

// Hasta la CL4.4 la conexión no descifra: un C3/C4 que llega se descarta con un aviso, sin cortar.
TEST_F(ConnectionTest, AnEncryptedPacketIsNotDelivered)
{
    OpenAndConnect();
    m_socket.reads = {Join({Bytes({0xC3, 0x05, 0x11, 0x22, 0x33}), Hello()})};

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_TRUE(HasWarning("C3"));
}

// Lo que llegó antes del FIN sale, y después el cierre.
TEST_F(ConnectionTest, APeerThatClosesCloses)
{
    OpenAndConnect();
    m_socket.reads = {Hello()};
    m_socket.peerClosed = true;

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"C1 04 00 01", "Closed(PeerClosed)"}));
    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::PeerClosed);
    EXPECT_FALSE(m_socket.IsOpen());
}

// Un Send no devuelve eventos: el cierre queda en el estado y sale en el próximo Poll.
TEST_F(ConnectionTest, ASendToAPeerThatClosedCloses)
{
    OpenAndConnect();
    m_socket.sendError = SocketError::Closed;

    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::PeerClosed);

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Closed(PeerClosed)"});
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
}

// En el Poll va primero el Flush: si falla a mitad (tomó 2 bytes y después dio el error), cierra y
// ese Poll ya no lee.
TEST_F(ConnectionTest, AFlushThatFailsInThePollClosesWithoutReading)
{
    OpenAndConnect();
    m_socket.sendLimits = {std::size_t{2}, std::nullopt};
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    m_socket.sendLimits = {std::size_t{1}, SocketError::Closed};
    m_socket.reads = {Hello()};

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Closed(PeerClosed)"});
    EXPECT_TRUE(SameBytes(m_socket.sent, Bytes({0xC1, 0x04, 0xF4})));
    EXPECT_EQ(m_socket.reads.size(), 1u);
    EXPECT_EQ(m_socket.closes, 1);
}

// Solo la conexión ve el socket: el error del sistema va al log con el cierre.
TEST_F(ConnectionTest, ASocketFailureLogsTheSystemError)
{
    OpenAndConnect();
    m_socket.sendError = SocketError::Failed;
    m_socket.systemError = SomeSystemError;

    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Closed(SocketFailed)"});
    EXPECT_TRUE(HasWarning(std::to_string(SomeSystemError)));
}

// Como el original, que cierra si lo pendiente más lo nuevo no entra en 0x2000 bytes.
TEST_F(ConnectionTest, AFullSendQueueCloses)
{
    OpenAndConnect();
    m_socket.sendLimits = {std::nullopt};

    m_connection.Send(PacketOf(C2Of(0x1FFE)), Delivery::Clear);
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    m_connection.Send(PacketOf(Bytes({0xC1, 0x03, 0x99})), Delivery::Clear);

    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::SendQueueFull);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Closed(SendQueueFull)"});
    EXPECT_TRUE(m_socket.sent.empty());
}

// Pasar del CS al GS: medio saludo del CS en el corte y un pedido que no salió no llegan a la
// conexión nueva. Sin vaciar el corte, el saludo del GS se cortaría pegado al pedazo del CS.
TEST_F(ConnectionTest, ReopeningStartsClean)
{
    OpenAndConnect();
    m_socket.sendLimits = {std::nullopt, std::nullopt};
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    m_socket.reads = {Bytes({0xC1, 0x04, 0x00})};
    ASSERT_EQ(Describe(m_connection.Poll()), Lines{});
    ASSERT_TRUE(m_socket.sent.empty());

    m_connection.Open(GameHost, GamePort);
    EXPECT_EQ(m_socket.closes, 1);
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
    m_socket.reads = {Hello()};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
    EXPECT_TRUE(m_socket.sent.empty());
    ASSERT_EQ(m_socket.connects.size(), 2u);
    EXPECT_EQ(m_socket.connects[1].host, GameHost);
    EXPECT_EQ(m_socket.connects[1].port, GamePort);
}

// Un cierre de la conexión de antes que nadie vio no sale en la nueva.
TEST_F(ConnectionTest, ReopeningForgetsACloseThatWasNotReported)
{
    OpenAndConnect();
    m_socket.sendError = SocketError::Closed;
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);
    ASSERT_EQ(m_connection.LastCloseReason(), CloseReason::PeerClosed);

    m_connection.Open(GameHost, GamePort);
    EXPECT_EQ(m_connection.LastCloseReason(), std::nullopt);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
}

// Close lo pide quien la usa, así que no vuelve como evento.
TEST_F(ConnectionTest, CloseIsNotAnEvent)
{
    OpenAndConnect();

    m_connection.Close();
    EXPECT_EQ(m_connection.State(), LinkState::Closed);
    EXPECT_EQ(m_connection.LastCloseReason(), CloseReason::ByClient);
    EXPECT_EQ(m_socket.closes, 1);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});

    m_connection.Close();
    EXPECT_EQ(m_socket.closes, 1);
}

// Cerrada, la conexión no toca el socket: con un Receive daría Closed, y con un Send, el error del
// guion.
TEST_F(ConnectionTest, AClosedConnectionDoesNotUseTheSocket)
{
    m_socket.reads = {Hello()};
    m_socket.sendError = SocketError::Failed;

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    m_connection.Send(PacketOf(ListRequest()), Delivery::Clear);

    EXPECT_EQ(m_socket.sendError, SocketError::Failed);
    EXPECT_EQ(m_socket.reads.size(), 1u);
    EXPECT_TRUE(HasWarning("C1 F4"));
}

// Hasta la CL4.4 la conexión no cifra: lo que va Encrypted no sale, con un aviso que dice el
// opcode.
TEST_F(ConnectionTest, AnEncryptedSendDoesNotLeave)
{
    OpenAndConnect();

    m_connection.Send(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x00})), Delivery::Encrypted);

    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_TRUE(HasWarning("C1 F1"));
}
} // namespace
