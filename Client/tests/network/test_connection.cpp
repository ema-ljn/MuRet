#include "Network/Connection.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Delivery.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/LinkState.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/SendQueue.h"
#include "Network/SocketError.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/ClaimsMoreSocket.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
// Por los literales (1ms), que include-cleaner 18 no asocia a <chrono>.
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Client::Network::Connection: el camino de un paquete en claro por una conexión (el plan de la
// CL4, CL4.3, paso 2), sobre un FakeSocket que sigue un guion y un FakeClock que no se mueve solo.
namespace
{
using namespace std::chrono_literals;
using Client::Core::Expected;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::Closed;
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;
using Client::Network::Connection;
using Client::Network::ConnectTimeout;
using Client::Network::Delivery;
using Client::Network::Event;
using Client::Network::FoxAntiObfuscation;
using Client::Network::HeaderSize;
using Client::Network::IdentityObfuscation;
using Client::Network::IObfuscation;
using Client::Network::LinkState;
using Client::Network::MaxPacketSize;
using Client::Network::MaxPendingSend;
using Client::Network::MaxReadsPerPoll;
using Client::Network::ObfuscationError;
using Client::Network::Opened;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::ReceiveChunkSize;
using Client::Network::SerialCheck;
using Client::Network::SlowConnectStart;
using Client::Network::SocketError;
using Client::Network::Undecryptable;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::CryptoErrorText;
using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::PlainBlockSize;
using Client::Protocol::Connection::WriteServerSelect;
using Client::Tests::AsTheGameServerReceives;
using Client::Tests::Bytes;
using Client::Tests::ClaimsMoreSocket;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerDecryption;
using Client::Tests::GameServerReceived;
using Client::Tests::PacketOf;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

// El ConnectServer y el GameServer de los tests: cualquier dirección, el FakeSocket no conecta.
constexpr std::string_view ConnectHost = "127.0.0.1";
constexpr std::uint16_t ConnectPort = 44405;
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// La conexión del ConnectServer, sin claves: con ella, un C3/C4 que llega es Undecryptable.
constexpr std::nullopt_t NoKeys = std::nullopt;

// La semilla del relleno de lo que se cifra: fija, para que un test dé siempre lo mismo.
constexpr std::uint32_t PaddingSeed = 1;

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
        else if (const Undecryptable* undecryptable = std::get_if<Undecryptable>(&event))
        {
            lines.push_back("Undecryptable(" +
                            (undecryptable->error ? ::testing::PrintToString(*undecryptable->error) : "sin claves") +
                            ")");
        }
        else
        {
            lines.push_back(HexOf(std::get<Packet>(event).Bytes()));
        }
    }
    return lines;
}

// El byte de relleno de un C3 de un solo bloque: el que sigue a lo útil, que el GameServer ve al
// descifrar el bloque. std::nullopt si no descifra o si lo útil llena el bloque.
std::optional<std::byte> PaddingOf(std::span<const std::byte> c3)
{
    const std::size_t header = HeaderSize(PacketType::C3);
    if (c3.size() != header + CipherBlockSize)
    {
        return std::nullopt;
    }
    const auto block = DecryptBlock(GameServerDecryption(), c3.subspan(header).first<CipherBlockSize>());
    if (!block || block->usefulSize >= PlainBlockSize)
    {
        return std::nullopt;
    }
    return block->bytes[block->usefulSize];
}

// Cuántos paquetes trae un lote, para los de miles de bytes.
std::size_t CountPackets(const std::vector<Event>& events)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(events, [](const Event& event) { return std::holds_alternative<Packet>(event); }));
}

// Una ofuscación que no toca nada y anota en `seen` cada etapa que la conexión le pide, con el tipo
// del paquete que ve ("AfterCut C3"): así un test mira en qué etapa un paquete va cifrado.
class SpyObfuscation final : public IObfuscation
{
public:
    explicit SpyObfuscation(Lines& seen) : m_seen(seen) {}

    [[nodiscard]] std::byte InboundMask(std::byte) const override
    {
        return std::byte{0};
    }
    [[nodiscard]] Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override
    {
        Note("AfterCut", packet);
        return packet;
    }
    [[nodiscard]] Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override
    {
        Note("BeforeDispatch", packet);
        return packet;
    }
    void BeforeEncrypt(std::span<std::byte>) const override
    {
        m_seen.emplace_back("BeforeEncrypt");
    }
    [[nodiscard]] Packet BeforeSend(Packet packet) const override
    {
        Note("BeforeSend", packet);
        return packet;
    }

private:
    void Note(std::string_view stage, const Packet& packet) const
    {
        m_seen.push_back(std::format("{} {:02X}", stage, static_cast<int>(packet.Type())));
    }

    Lines& m_seen;
};

// La conexión guarda referencias: una ofuscación temporal no compila, porque moriría antes, y no se
// copia ni se mueve.
static_assert(!std::is_constructible_v<Connection, FakeSocket&, FoxAntiObfuscation, FakeClock&, Log&, std::uint32_t>);
static_assert(
    std::is_constructible_v<Connection, FakeSocket&, const FoxAntiObfuscation&, FakeClock&, Log&, std::uint32_t>);
static_assert(!std::is_copy_constructible_v<Connection>);
static_assert(!std::is_move_constructible_v<Connection>);

class ConnectionTest : public ::testing::Test
{
protected:
    // Open al ConnectServer y el Poll que da el Opened.
    void OpenAndConnect(Connection& connection)
    {
        connection.Open(ConnectHost, ConnectPort, NoKeys);
        ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
    }

    void OpenAndConnect()
    {
        OpenAndConnect(m_connection);
    }

    // Open al GameServer, con las claves, y el Poll que da el Opened.
    void OpenTheGameServer(Connection& connection)
    {
        connection.Open(GameHost, GamePort, GameClientKeys());
        ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
    }

    void OpenTheGameServer()
    {
        OpenTheGameServer(m_connection);
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

    // Las líneas de aviso, enteras: un test que las compara así ve también que no traen los bytes
    // del paquete, que en el login llevan la cuenta y la clave.
    [[nodiscard]] Lines Warnings() const
    {
        Lines warnings;
        for (std::size_t index = 0; index < m_sink.lines.size(); ++index)
        {
            if (m_sink.levels[index] == LogLevel::Warning)
            {
                warnings.push_back(m_sink.lines[index]);
            }
        }
        return warnings;
    }

    FakeSocket m_socket;
    FakeClock m_clock;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
    Connection m_connection{m_socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed};
};

TEST_F(ConnectionTest, OpensWhenTheSocketConnects)
{
    m_socket.pollsUntilConnected = 2;

    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
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
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
}

TEST_F(ConnectionTest, OpenedComesBeforeWhatArrivesWithIt)
{
    m_socket.pollsUntilConnected = 1;
    m_socket.reads = {Hello()};
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
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
        Connection connection(socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed);

        connection.Open(ConnectHost, ConnectPort, NoKeys);
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
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);

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
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);

    m_clock.Advance(60s);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    EXPECT_EQ(m_connection.State(), LinkState::Open);
}

// El tope es del connect: lo que tardó el resolver adentro de Open no cuenta.
TEST_F(ConnectionTest, TheConnectTimeoutStartsAfterTheResolve)
{
    m_socket.pollsUntilConnected = std::numeric_limits<int>::max();
    m_socket.onStartConnect = [this] { m_clock.Advance(5s); };
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);

    m_clock.Advance(ConnectTimeout - 1ms);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(m_connection.State(), LinkState::Connecting);
}

// Pregunta 5 del plan: el resolver del sistema espera, y lo que tarda va al log si pasa de 100 ms.
TEST_F(ConnectionTest, ASlowResolveIsLogged)
{
    m_socket.onStartConnect = [this] { m_clock.Advance(SlowConnectStart); };
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
    EXPECT_FALSE(HasWarning("tardó"));

    m_socket.onStartConnect = [this] { m_clock.Advance(SlowConnectStart + 1ms); };
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
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
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);
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
    m_connection.Open(ConnectHost, ConnectPort, NoKeys);

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

// Un socket que dice que leyó más de lo que entra en el buffer no hace leer afuera: se corta lo que
// hay en el buffer. Termina con medio C1 de 7 bytes, que la lectura siguiente completa; si se
// leyeran los 5 de más, el C1 se completaría con lo que haya detrás del buffer.
TEST_F(ConnectionTest, ASocketThatClaimsMoreThanTheBufferDoesNotReadPastIt)
{
    ClaimsMoreSocket socket;
    socket.first = Join({C2Of(ReceiveChunkSize - 2), Bytes({0xC1, 0x07})});
    socket.reads = {Bytes({0x99, 0x00, 0x00, 0x00, 0x00})};
    Connection connection(socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed);
    connection.Open(ConnectHost, ConnectPort, NoKeys);

    const std::vector<Event> events = connection.Poll();

    ASSERT_EQ(events.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Opened>(events[0]));
    ASSERT_TRUE(std::holds_alternative<Packet>(events[1]));
    EXPECT_TRUE(SameBytes(std::get<Packet>(events[1]).Bytes(), C2Of(ReceiveChunkSize - 2)));
    EXPECT_EQ(Describe({events[2]}), Lines{"C1 07 99 00 00 00 00"});
    EXPECT_EQ(connection.State(), LinkState::Open);
}

// 04 § El flujo de conexión: la lista del laboratorio llega al cable como F4 02, y FoxAnti la pasa
// a la F4 06 que lee main.exe, con el código 400.
TEST_F(ConnectionTest, WithFoxAntiTheListArrivesAsF406)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenAndConnect(connection);
    m_socket.reads = {Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C2 00 0B F4 06 00 01 90 01 00 00"});
}

// Y la elección del 400 vuelve al código del laboratorio en el BeforeSend.
TEST_F(ConnectionTest, WithFoxAntiTheChoiceLeavesAsF40300)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
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
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenAndConnect(connection);
    m_socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 06 F3 11 07 00"});
}

// Un F3 11 que termina antes de pkt[4] no se traduce en BeforeDispatch (FoxAnti no valida los
// largos, Q-C09): se descarta con un aviso que dice el tipo y el opcode, y la conexión sigue.
TEST_F(ConnectionTest, WithFoxAntiASkillListThatDoesNotTranslateIsDropped)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenAndConnect(connection);
    m_socket.reads = {Join({Bytes({0xC1, 0x04, 0xF3, 0x11}), Hello()})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(connection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C1 F3 que llegó: la ofuscación no lo traduce"});
}

// El GameServer del laboratorio manda cada byte con el 0xAA (03 § La capa de FoxAnti): el corte lo
// saca con la máscara de la ofuscación de la conexión, también después de volver a abrir.
TEST_F(ConnectionTest, WithFoxAntiWhatArrivesFromTheGameServerIsUnmasked)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    const ByteVector maskedHello = Bytes({0xC1 ^ 0xAA, 0x04 ^ 0xAA, 0x00 ^ 0xAA, 0x01 ^ 0xAA});
    OpenAndConnect(connection);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});

    connection.Open(GameHost, GamePort, NoKeys);
    m_socket.reads = {maskedHello};
    EXPECT_EQ(Describe(connection.Poll()), (Lines{"Opened", "C1 04 00 01"}));
}

// Un InvalidPacket de la ofuscación no corta: el largo lo dio la cabecera, así que el siguiente se
// sabe dónde empieza. Esta lista dice dos entradas y trae una.
TEST_F(ConnectionTest, AListThatDoesNotTranslateIsDropped)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenAndConnect(connection);
    m_socket.reads = {Join({Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x02, 0x00, 0x00, 0x00, 0xCC}), Hello()})};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(connection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C2 F4 que llegó: la ofuscación no lo traduce"});
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

// La conexión del ConnectServer no tiene claves: un C3/C4 que llega es Undecryptable, sin error,
// con un aviso, y la conexión sigue.
TEST_F(ConnectionTest, AnEncryptedPacketWithoutKeysIsUndecryptable)
{
    OpenAndConnect();
    m_socket.reads = {Join({Bytes({0xC3, 0x05, 0x11, 0x22, 0x33}), Hello()})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Undecryptable(sin claves)", "C1 04 00 01"}));
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C3 que llegó: esta conexión no tiene claves"});
}

// Con las claves, un C1 Encrypted sale como C3: el GameServer lo descifra con la suya y es el mismo
// C1, con el serial 0, y el siguiente con el 1. Lo que va Clear sale igual que sin claves.
TEST_F(ConnectionTest, EncryptsWhatGoesEncrypted)
{
    OpenTheGameServer();
    const ByteVector first = Bytes({0xC1, 0x06, 0x0E, 0x00, 0x11, 0x22});
    const ByteVector second = Bytes({0xC1, 0x05, 0xF1, 0x01, 0x33});

    m_connection.Send(PacketOf(first), Delivery::Encrypted);
    m_connection.Send(PacketOf(Hello()), Delivery::Clear);
    m_connection.Send(PacketOf(second), Delivery::Encrypted);

    ASSERT_FALSE(m_socket.sent.empty());
    EXPECT_EQ(m_socket.sent[0], std::byte{0xC3});
    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 3u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), first));
    EXPECT_TRUE(SameBytes(received.packets[1].Bytes(), Hello()));
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[2].Bytes(), second));
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    EXPECT_EQ(Warnings(), Lines{});
}

// Lo que se cifra mientras conecta toma su serial al mandarlo, y sale con el Opened.
TEST_F(ConnectionTest, WhatIsEncryptedWhileConnectingLeavesWhenItOpens)
{
    m_socket.pollsUntilConnected = 1;
    m_connection.Open(GameHost, GamePort, GameClientKeys());
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
    ASSERT_TRUE(m_socket.sent.empty());

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{});
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), Bytes({0xC1, 0x04, 0x0E, 0x00})));
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
}

// Un C3 del GameServer sale como el C1 rearmado, sin el serial, y uno que llega como C4, como C2.
TEST_F(ConnectionTest, DecryptsWhatArrivesEncrypted)
{
    OpenTheGameServer();
    PacketSerials server;
    m_socket.reads = {Join({FromTheGameServer(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}), server),
                            FromTheGameServer(Bytes({0xC2, 0x00, 0x06, 0xF3, 0x00, 0x02}), server)})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"C1 05 F1 01 01", "C2 00 06 F3 00 02"}));
    EXPECT_EQ(Warnings(), Lines{});
}

// Cada Open vuelve los dos seriales a 0, como 0x4c1340 al conectar al GameServer: lo que sale
// después arranca en 0, y lo que llega con el 0 está en orden.
TEST_F(ConnectionTest, OpeningAgainResetsTheSerials)
{
    OpenTheGameServer();
    PacketSerials before;
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
    m_socket.reads = {Join({FromTheGameServer(Hello(), before), FromTheGameServer(Hello(), before)})};
    ASSERT_EQ(Describe(m_connection.Poll()), (Lines{"C1 04 00 01", "C1 04 00 01"}));

    m_connection.Open(GameHost, GamePort, GameClientKeys());
    ASSERT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    const std::size_t sentBefore = m_socket.sent.size();
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
    PacketSerials after;
    m_socket.reads = {FromTheGameServer(Hello(), after)};

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"C1 04 00 01"});
    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(std::span(m_socket.sent).subspan(sentBefore), server);
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
    EXPECT_EQ(Warnings(), Lines{});
}

// Volver a abrir sin claves se olvida de las de antes: un C3 que llega es Undecryptable, y lo que
// va Encrypted no sale.
TEST_F(ConnectionTest, OpeningWithoutKeysForgetsTheOldOnes)
{
    OpenTheGameServer();
    m_connection.Open(GameHost, GamePort, NoKeys);
    ASSERT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
    PacketSerials server;
    m_socket.reads = {FromTheGameServer(Hello(), server)};

    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Undecryptable(sin claves)"});
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(Warnings(), (Lines{"[warning] se descarta un C3 que llegó: esta conexión no tiene claves",
                                 "[warning] se descarta un C1 0E que va cifrado: esta conexión no tiene claves"}));
}

// Un checksum malo (el byte 10 del primer bloque, Q-C04): Undecryptable con ChecksumMismatch, y el
// C3 bueno que sigue, con el serial siguiente, se despacha en orden: el malo gastó su serial.
TEST_F(ConnectionTest, ABadChecksumIsUndecryptable)
{
    OpenTheGameServer();
    PacketSerials server;
    ByteVector bad = FromTheGameServer(Hello(), server);
    const std::size_t firstChecksum = HeaderSize(PacketType::C3) + CipherBlockSize - 1;
    ASSERT_GT(bad.size(), firstChecksum);
    bad[firstChecksum] ^= std::byte{0xFF};
    m_socket.reads = {Join({bad, FromTheGameServer(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}), server)})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Undecryptable(ChecksumMismatch)", "C1 05 F1 01 01"}));
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{std::format("[warning] se descarta un C3 que llegó: no se descifra ({})",
                                            CryptoErrorText(CryptoError::ChecksumMismatch))});
}

// Lo cifrado tiene que medir bloques enteros de 11: uno de 5 es NotWholeBlocks.
TEST_F(ConnectionTest, AnEncryptedPacketThatIsNotWholeBlocksIsUndecryptable)
{
    OpenTheGameServer();
    m_socket.reads = {Join({Bytes({0xC3, 0x07, 0x11, 0x22, 0x33, 0x44, 0x55}), Hello()})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"Undecryptable(NotWholeBlocks)", "C1 04 00 01"}));
    EXPECT_EQ(m_connection.State(), LinkState::Open);
}

// Un serial que no es el que se esperaba (Q-C04): el paquete se despacha, el log lo anota una vez
// y desde ahí se sigue el que trajo, así el siguiente está en orden.
TEST_F(ConnectionTest, ASerialOutOfOrderIsResyncedOnce)
{
    OpenTheGameServer();
    PacketSerials server;
    const ByteVector first = FromTheGameServer(Hello(), server);
    for (int skipped = 0; skipped < 3; ++skipped)
    {
        [[maybe_unused]] const std::uint8_t serial = server.TakeSend();
    }
    m_socket.reads = {Join({first, FromTheGameServer(Hello(), server), FromTheGameServer(Hello(), server)})};

    EXPECT_EQ(Describe(m_connection.Poll()), (Lines{"C1 04 00 01", "C1 04 00 01", "C1 04 00 01"}));
    EXPECT_EQ(Warnings(), Lines{"[warning] un C3 que llegó trae otro serial que el que se esperaba: se "
                                "sigue desde el suyo"});
}

// El GameServer del laboratorio manda también los C3 con el 0xAA de FoxAnti: el corte lo saca y
// después se descifra. Y lo descifrado pasa por BeforeDispatch: un F3 11 recibe su byte.
TEST_F(ConnectionTest, WithFoxAntiWhatArrivesEncryptedIsUnmaskedDecryptedAndTranslated)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenTheGameServer(connection);
    PacketSerials server;
    ByteVector masked =
        Join({FromTheGameServer(Hello(), server), FromTheGameServer(Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07}), server)});
    for (std::byte& value : masked)
    {
        value ^= std::byte{0xAA};
    }
    m_socket.reads = {masked};

    EXPECT_EQ(Describe(connection.Poll()), (Lines{"C1 04 00 01", "C1 06 F3 11 07 00"}));
    EXPECT_EQ(Warnings(), Lines{});
}

// Lo que se cifra pasa por el BeforeEncrypt de la ofuscación: FoxAnti pasa el opcode 0x10 a 0x1D
// (03 § La capa de FoxAnti), y el GameServer lo ve así al descifrar.
TEST_F(ConnectionTest, WithFoxAntiWhatIsEncryptedIsRemapped)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenTheGameServer(connection);

    connection.Send(PacketOf(Bytes({0xC1, 0x05, 0x10, 0x11, 0x22})), Delivery::Encrypted);

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), std::optional<std::uint8_t>{std::uint8_t{0x1D}});
}

// En el orden del original (03, "Cadena de recepción"): AfterCut ve el C3 todavía cifrado, y
// BeforeDispatch, el C1 ya descifrado. Con FoxAnti no se ve, porque su AfterCut solo toca la lista
// del ConnectServer.
TEST_F(ConnectionTest, WhatArrivesIsDecryptedBetweenAfterCutAndBeforeDispatch)
{
    Lines seen;
    const SpyObfuscation spy(seen);
    Connection connection(m_socket, spy, m_clock, m_log, PaddingSeed);
    OpenTheGameServer(connection);
    PacketSerials server;
    m_socket.reads = {FromTheGameServer(Hello(), server)};

    EXPECT_EQ(Describe(connection.Poll()), Lines{"C1 04 00 01"});
    EXPECT_EQ(seen, (Lines{"AfterCut C3", "BeforeDispatch C1"}));
}

// Al mandar, BeforeEncrypt va adentro del cifrado y BeforeSend después: ve el C3.
TEST_F(ConnectionTest, WhatIsEncryptedIsEncryptedBeforeBeforeSend)
{
    Lines seen;
    const SpyObfuscation spy(seen);
    Connection connection(m_socket, spy, m_clock, m_log, PaddingSeed);
    OpenTheGameServer(connection);

    connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);

    EXPECT_EQ(seen, (Lines{"BeforeEncrypt", "BeforeSend C3"}));
}

// El BeforeSend de FoxAnti pasa un C1 en claro con D3 a A3 (03 § La capa de FoxAnti), pero deja
// los C3/C4 como están (encode_outbound): un D3 que va cifrado le llega al GameServer como D3.
TEST_F(ConnectionTest, WithFoxAntiAnEncryptedD3IsNotRemapped)
{
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    OpenTheGameServer(connection);

    connection.Send(PacketOf(Bytes({0xC1, 0x05, 0xD3, 0x11, 0x22})), Delivery::Encrypted);

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.packets[0].Opcode(), std::optional<std::uint8_t>{std::uint8_t{0xD3}});
}

// El relleno de cada paquete cifrado es el byte de abajo de lo que da un std::mt19937 con la
// semilla de la conexión, uno por paquete: dos conexiones con la misma semilla dan lo mismo.
TEST_F(ConnectionTest, ThePaddingComesFromTheSeededGenerator)
{
    OpenTheGameServer();
    std::mt19937 expected(PaddingSeed);

    for (int packet = 0; packet < 4; ++packet)
    {
        const std::size_t before = m_socket.sent.size();
        m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);
        const std::optional<std::byte> padding = PaddingOf(std::span(m_socket.sent).subspan(before));
        EXPECT_EQ(padding, std::optional<std::byte>{static_cast<std::byte>(expected() & 0xFFu)}) << packet;
    }
}

// Un paquete que EncryptPacket no puede cifrar (un C2 que como C4 pasaría de MaxPacketSize) no
// sale, con un aviso, y no gasta un serial: el siguiente sale con el 0.
TEST_F(ConnectionTest, APacketThatDoesNotEncryptDoesNotLeave)
{
    OpenTheGameServer();

    m_connection.Send(PacketOf(C2Of(6000)), Delivery::Encrypted);
    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    m_connection.Send(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00})), Delivery::Encrypted);

    PacketSerials server;
    const GameServerReceived received = AsTheGameServerReceives(m_socket.sent, server);
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
    EXPECT_EQ(Warnings(), Lines{std::format("[warning] no se cifra un C2 99 que iba a salir: {}",
                                            CryptoErrorText(CryptoError::TooLarge))});
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
    EXPECT_EQ(Warnings(),
              Lines{std::format("[warning] la conexión se cerró: {}", CloseReasonText(CloseReason::PeerClosed))});
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

// Cada motivo de cierre deja una sola línea en el log, entera y con su nivel: ByClient en info,
// porque lo pidió quien usa la conexión, y el resto en warning; SocketFailed, con el error del
// sistema. Una fila por motivo de CloseReason, como en CloseReasonTest.EveryReasonHasItsOwnText,
// cada una con su conexión.
TEST_F(ConnectionTest, EveryCloseIsLoggedWithItsLevel)
{
    struct Rig
    {
        FakeSocket socket;
        FakeClock clock;
        RecordingSink sink;
        Log log{sink, LogLevel::Debug};
        Connection connection{socket, IdentityObfuscation::Shared(), clock, log, PaddingSeed};

        void OpenAndConnect()
        {
            connection.Open(ConnectHost, ConnectPort, NoKeys);
            ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
        }
    };
    struct Case
    {
        CloseReason reason;
        std::string line;
        LogLevel level;
        std::function<void(Rig&)> close;
    };
    const auto closed = [](CloseReason reason)
    { return std::format("la conexión se cerró: {}", CloseReasonText(reason)); };
    const Case cases[] = {
        {CloseReason::ByClient, "[info] " + closed(CloseReason::ByClient), LogLevel::Info,
         [](Rig& rig)
         {
             rig.connection.Open(ConnectHost, ConnectPort, NoKeys);
             rig.connection.Close();
         }},
        {CloseReason::ResolveFailed, "[warning] " + closed(CloseReason::ResolveFailed), LogLevel::Warning,
         [](Rig& rig)
         {
             rig.socket.connectResult = SocketError::ResolveFailed;
             rig.connection.Open(ConnectHost, ConnectPort, NoKeys);
         }},
        {CloseReason::Refused, "[warning] " + closed(CloseReason::Refused), LogLevel::Warning,
         [](Rig& rig)
         {
             rig.socket.connectResult = SocketError::Refused;
             rig.connection.Open(ConnectHost, ConnectPort, NoKeys);
         }},
        {CloseReason::ConnectTimedOut, "[warning] " + closed(CloseReason::ConnectTimedOut), LogLevel::Warning,
         [](Rig& rig)
         {
             rig.socket.pollsUntilConnected = std::numeric_limits<int>::max();
             rig.connection.Open(ConnectHost, ConnectPort, NoKeys);
             rig.clock.Advance(ConnectTimeout);
             [[maybe_unused]] const std::vector<Event> events = rig.connection.Poll();
         }},
        {CloseReason::PeerClosed, "[warning] " + closed(CloseReason::PeerClosed), LogLevel::Warning,
         [](Rig& rig)
         {
             ASSERT_NO_FATAL_FAILURE(rig.OpenAndConnect());
             rig.socket.peerClosed = true;
             [[maybe_unused]] const std::vector<Event> events = rig.connection.Poll();
         }},
        {CloseReason::SocketFailed,
         std::format("[warning] {} (error {} del sistema)", closed(CloseReason::SocketFailed), SomeSystemError),
         LogLevel::Warning,
         [](Rig& rig)
         {
             ASSERT_NO_FATAL_FAILURE(rig.OpenAndConnect());
             rig.socket.sendError = SocketError::Failed;
             rig.socket.systemError = SomeSystemError;
             rig.connection.Send(PacketOf(ListRequest()), Delivery::Clear);
         }},
        {CloseReason::BadStream, "[warning] " + closed(CloseReason::BadStream), LogLevel::Warning,
         [](Rig& rig)
         {
             ASSERT_NO_FATAL_FAILURE(rig.OpenAndConnect());
             rig.socket.reads = {Bytes({0xFF, 0x00})};
             [[maybe_unused]] const std::vector<Event> events = rig.connection.Poll();
         }},
        {CloseReason::SendQueueFull, "[warning] " + closed(CloseReason::SendQueueFull), LogLevel::Warning,
         [](Rig& rig)
         {
             ASSERT_NO_FATAL_FAILURE(rig.OpenAndConnect());
             rig.socket.sendLimits = {std::nullopt};
             rig.connection.Send(PacketOf(C2Of(MaxPendingSend - 2)), Delivery::Clear);
             rig.connection.Send(PacketOf(Bytes({0xC1, 0x03, 0x99})), Delivery::Clear);
         }},
    };
    for (const Case& row : cases)
    {
        SCOPED_TRACE(::testing::PrintToString(row.reason));
        Rig rig;
        row.close(rig);

        EXPECT_EQ(rig.connection.LastCloseReason(), row.reason);
        EXPECT_EQ(rig.sink.lines, Lines{row.line});
        EXPECT_EQ(rig.sink.levels, std::vector<LogLevel>{row.level});
    }
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

    m_connection.Open(GameHost, GamePort, NoKeys);
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

    m_connection.Open(GameHost, GamePort, NoKeys);
    EXPECT_EQ(m_connection.LastCloseReason(), std::nullopt);
    EXPECT_EQ(Describe(m_connection.Poll()), Lines{"Opened"});
}

// Close lo pide quien la usa, así que no vuelve como evento, y va al log como info, no como aviso.
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
    EXPECT_EQ(m_sink.lines,
              Lines{std::format("[info] la conexión se cerró: {}", CloseReasonText(CloseReason::ByClient))});
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
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C1 F4 que iba a salir: la conexión está cerrada"});
}

// Sin claves, lo que va Encrypted no sale, con un aviso que dice el tipo y el opcode y nada más.
// Un login (C1 F1 01) a una conexión sin claves llevaría la cuenta y la clave con el XOR32: la
// línea se compara entera.
TEST_F(ConnectionTest, AnEncryptedSendWithoutKeysDoesNotLeave)
{
    OpenAndConnect();

    m_connection.Send(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x00})), Delivery::Encrypted);

    EXPECT_TRUE(m_socket.sent.empty());
    EXPECT_EQ(m_connection.State(), LinkState::Open);
    EXPECT_EQ(Warnings(), Lines{"[warning] se descarta un C1 F1 que va cifrado: esta conexión no tiene claves"});
}
} // namespace
