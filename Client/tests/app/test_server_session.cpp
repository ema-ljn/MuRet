#include "App/ServerSession.h"

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/LinkState.h"
#include "Network/SocketError.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::App::ServerSession con el ConnectServer, hasta la dirección del GameServer (el plan de
// la CL4, CL4.3, paso 4), sobre una Connection de verdad y un FakeSocket que hace de los dos
// servidores. Los bytes son los del 04 (El flujo de conexión), escritos a mano; las direcciones,
// unas cualquiera, porque el FakeSocket no conecta.
namespace
{
using Client::App::Credentials;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionFailure;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;
using Client::Network::Connection;
using Client::Network::FoxAntiObfuscation;
using Client::Network::IdentityObfuscation;
using Client::Network::IObfuscation;
using Client::Network::LinkState;
using Client::Network::SocketError;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

constexpr std::string_view ConnectHost = "10.0.0.1";
constexpr std::uint16_t ConnectPort = 44405;
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// Lo que mide el host en la respuesta F4 03 (04 § El flujo de conexión).
constexpr std::size_t AddressHostSize = 16;

// El código de servidor del 0 del laboratorio, como lo deja FoxAnti (el 0 más su delta, 400).
constexpr std::uint16_t LabServerCode = 400;

// El pedido de la información de la mascota, como llega al cable con su XOR32.
ByteVector PetInfoRequest()
{
    return Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F});
}

ByteVector Hello()
{
    return Bytes({0xC1, 0x04, 0x00, 0x01});
}

ByteVector ListRequest()
{
    return Bytes({0xC1, 0x04, 0xF4, 0x02});
}

// Pega varios paquetes, como llegan en una lectura o como salen en `sent`.
ByteVector Join(std::initializer_list<ByteVector> parts)
{
    ByteVector joined;
    for (const ByteVector& part : parts)
    {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

// La lista como la lee main.exe (F4 06): la cantidad en dos bytes big-endian y, por servidor, el
// código en little-endian, la carga y un byte que no se lee.
ByteVector ServerList(std::initializer_list<std::uint16_t> codes)
{
    const std::size_t size = 7 + 4 * codes.size();
    ByteVector bytes = Bytes({0xC2, static_cast<int>(size >> 8), static_cast<int>(size & 0xFF), 0xF4, 0x06,
                              static_cast<int>(codes.size() >> 8), static_cast<int>(codes.size() & 0xFF)});
    for (const std::uint16_t code : codes)
    {
        const ByteVector entry = Bytes({code & 0xFF, code >> 8, 0x00, 0x00});
        bytes.insert(bytes.end(), entry.begin(), entry.end());
    }
    return bytes;
}

// La lista del laboratorio como llega al cable, antes de que FoxAnti la pase a F4 06: un servidor.
ByteVector LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
}

// La respuesta F4 03: el host en 16 bytes con \0 detrás y el puerto en little-endian.
ByteVector AddressReply(std::string_view host, std::uint16_t port)
{
    ByteVector bytes = Bytes({0xC1, 0x16, 0xF4, 0x03});
    const std::span<const std::byte> text = BytesOf(host);
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.resize(4 + AddressHostSize);
    const ByteVector portBytes = Bytes({port & 0xFF, port >> 8});
    bytes.insert(bytes.end(), portBytes.begin(), portBytes.end());
    return bytes;
}

SessionSettings Settings()
{
    return SessionSettings{{std::string(ConnectHost), ConnectPort}, Credentials{"user0001", "pass0002"}};
}

// Una sesión con su conexión y lo que la rodea. La ofuscación es de quien la arma y vive más.
struct Harness
{
    explicit Harness(const IObfuscation& obfuscation) : connection(socket, obfuscation, clock, log) {}

    // Las líneas de aviso, enteras.
    [[nodiscard]] Lines Warnings() const
    {
        Lines warnings;
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == LogLevel::Warning)
            {
                warnings.push_back(sink.lines[index]);
            }
        }
        return warnings;
    }

    // Si alguna línea de `level` tiene `part`.
    [[nodiscard]] bool Logged(LogLevel level, std::string_view part) const
    {
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    // Start y el Poll del saludo, que llega con el connect: espera la lista.
    void ReachServerList()
    {
        session.Start();
        socket.reads = {Hello()};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
    }

    // Y la lista, de un servidor: espera la dirección.
    void ReachServerAddress()
    {
        ReachServerList();
        socket.reads = {ServerList({LabServerCode})};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingServerAddress);
    }

    // Y la dirección: conecta al GameServer.
    void ReachGameServer()
    {
        ReachServerAddress();
        socket.reads = {AddressReply(GameHost, GamePort)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::ConnectingGameServer);
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    ServerSession session{connection, log, Settings()};
};

TEST(ServerSessionTest, StartsIdle)
{
    Harness tested(IdentityObfuscation::Shared());

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Idle);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_TRUE(tested.socket.connects.empty());
}

// Lo primero que sale al ConnectServer es el A9, y recién cuando el connect termina.
TEST(ServerSessionTest, SendsThePetInfoRequestWhenTheConnectServerOpens)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.socket.pollsUntilConnected = 1;

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    ASSERT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.socket.connects[0].host, ConnectHost);
    EXPECT_EQ(tested.socket.connects[0].port, ConnectPort);
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.socket.sent.empty());

    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, PetInfoRequest()));
}

// El test del 04: después de C1 04 00 01 sale C1 04 F4 02, exacto.
TEST(ServerSessionTest, AsksForTheListAfterTheHello)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
}

// El Opened va antes que lo que llegó con él: el A9 sale antes que el F4 02 aunque el saludo llegue
// en el mismo Poll que el connect.
TEST(ServerSessionTest, ThePetInfoRequestLeavesBeforeAHelloThatArrivesWithTheConnect)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.socket.reads = {Hello()};

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
}

// La lista del laboratorio, con FoxAnti: la conexión la pasa a F4 06, la sesión elige el código
// 400, y FoxAnti lo pasa de vuelta al 0 del ConnectServer.
TEST(ServerSessionTest, ChoosesTheFirstServer)
{
    const FoxAntiObfuscation foxAnti;
    Harness tested(foxAnti);
    tested.session.Start();
    tested.socket.reads = {Hello()};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerList);

    tested.socket.reads = {LabServerList()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00})})));
}

// De varios, el primero en el orden en que llegan (pregunta 9), no el de código más bajo.
TEST(ServerSessionTest, ChoosesTheFirstOfSeveral)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {ServerList({401, 400, 402})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x91, 0x01})})));
}

// La dirección del F4 03: la sesión la guarda, cierra el CS y conecta al GS, sin avisos.
TEST(ServerSessionTest, ReadsTheGameServerAddress)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerAddress();

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    ASSERT_TRUE(tested.session.GameServer().has_value());
    const ServerAddress gameServer = tested.session.GameServer().value_or(ServerAddress{});
    EXPECT_EQ(gameServer.host, GameHost);
    EXPECT_EQ(gameServer.port, GamePort);
    EXPECT_EQ(tested.socket.closes, 1);
    ASSERT_EQ(tested.socket.connects.size(), 2u);
    EXPECT_EQ(tested.socket.connects[1].host, GameHost);
    EXPECT_EQ(tested.socket.connects[1].port, GamePort);
    EXPECT_EQ(tested.connection.State(), LinkState::Connecting);
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// El F4 03 y un saludo en la misma lectura: el saludo es del CS y la sesión deja de recorrer el
// lote. Si lo despachara, iría al dispatcher del GS, donde el 00 es el chat y no tiene handler, y
// habría un aviso. Cuando el GS abre, no sale nada: el saludo lo manda él (CL4.4).
TEST(ServerSessionTest, WhatFollowsTheAddressIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerAddress();
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Join({AddressReply(GameHost, GamePort), Hello()})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);

    tested.session.Poll();
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Con el GameServer, el 00 va a su dispatcher, que hasta la CL4.4 no tiene handlers: no es el
// saludo del CS ni pide otra lista.
TEST(ServerSessionTest, WithTheGameServerThe00IsNotTheConnectHello)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachGameServer();
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Hello()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] un paquete con el opcode 00 no tiene handler: se descarta, y este aviso no se repite"});
}

// Pregunta 9: una lista vacía es un error que lo dice, y la sesión cierra el CS.
TEST(ServerSessionTest, AnEmptyListFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {Bytes({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x00})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.closes, 1);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la lista de servidores está vacía"));

    // El cierre lo pidió la sesión: no vuelve como un ConnectServerLost.
    tested.session.Poll();
    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
}

// La lista vacía y el cierre del CS en el mismo Poll: la sesión falla por la lista, que llegó
// primero, y deja de recorrer el lote.
TEST(ServerSessionTest, AnEmptyListBeforeTheCloseKeepsItsFailure)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {ServerList({})};
    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
}

// Un F4 03 antes de la lista no es la dirección: se ignora con un aviso, sin conectar a nada.
TEST(ServerSessionTest, AnUnexpectedPacketIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] se ignora la dirección del GameServer (F4 03): llegó fuera de turno"});
}

// En 8 de 64 conexiones el original pide la lista dos veces (04 § El flujo de conexión): acá, un
// segundo saludo se ignora con un aviso y el F4 02 sale una sola vez.
TEST(ServerSessionTest, ASecondHelloIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {Hello()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora el saludo del ConnectServer (00): llegó fuera de turno"});
}

// Una segunda lista, ya elegido el servidor, no elige otro.
TEST(ServerSessionTest, AListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerAddress();
    const ByteVector sent = tested.socket.sent;

    tested.socket.reads = {ServerList({401})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la lista de servidores (F4 06): llegó fuera de turno"});
}

// Una lista que dice dos servidores y trae uno no se lee: se ignora con un aviso, y la sesión
// sigue esperando una que sirva.
TEST(ServerSessionTest, AListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x02, 0x90, 0x01, 0x00, 0x00})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la lista de servidores (F4 06): no se puede leer"});
}

// Lo mismo con una dirección que termina antes del puerto.
TEST(ServerSessionTest, AnAddressThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerAddress();

    tested.socket.reads = {Bytes({0xC1, 0x06, 0xF4, 0x03, 0x31, 0x00})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la dirección del GameServer (F4 03): no se puede leer"});
}

// Un F4 que termina en el opcode no tiene subcódigo: el Dispatcher da Malformed sin avisar, y el
// aviso lo pone la sesión, sin los bytes.
TEST(ServerSessionTest, AMalformedPacketIsLogged)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.reads = {Bytes({0xC1, 0x03, 0xF4})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un paquete mal formado del ConnectServer"});
}

TEST(ServerSessionTest, LosingTheConnectServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();

    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el ConnectServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
}

// También mientras conecta: un CS que rechaza la conexión.
TEST(ServerSessionTest, AConnectServerThatRefusesFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.socket.connectResult = SocketError::Refused;

    tested.session.Start();
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
    EXPECT_TRUE(tested.socket.sent.empty());
}

// El GameServer que dio el CS rechaza la conexión.
TEST(ServerSessionTest, LosingTheGameServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerAddress();
    tested.socket.connectResult = SocketError::Refused;

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::Refused))));
}

// Un Send desde un handler puede cerrar la conexión (acá, el F4 02 a un CS que ya cerró): lo que
// queda del lote no se despacha, y el cierre sale en el Poll siguiente.
TEST(ServerSessionTest, WhatFollowsACloseInAHandlerIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.sendError = SocketError::Closed;
    tested.socket.reads = {Join({Hello(), ServerList({LabServerCode})})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "se descarta un C1 F4 que iba a salir"));

    tested.session.Poll();
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Si el CS cierra en el mismo Poll en que llega un paquete, el paquete se despacha y después el
// cierre: la sesión no se queda esperando una lista que no va a llegar.
TEST(ServerSessionTest, WhatArrivesBeforeTheServerClosesIsHandled)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Start vuelve a empezar desde cualquier estado, y olvida la falla y el GameServer de antes.
TEST(ServerSessionTest, StartingAgainStartsClean)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.ReachServerList();
    tested.socket.reads = {ServerList({})};
    tested.session.Poll();
    ASSERT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    tested.socket.reads = {Hello(), ServerList({LabServerCode}), AddressReply(GameHost, GamePort)};
    tested.session.Poll();
    tested.session.Poll();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    ASSERT_EQ(tested.socket.connects.size(), 4u);
    EXPECT_EQ(tested.socket.connects[3].host, ConnectHost);
    EXPECT_EQ(tested.socket.connects[3].port, ConnectPort);
    const ByteVector before = tested.socket.sent;
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(std::span(tested.socket.sent).subspan(before.size()), PetInfoRequest()));
}
} // namespace
