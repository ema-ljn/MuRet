#include "App/ServerSession.h"

#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ServerAddress.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/SocketError.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Login.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ServerSession con el ConnectServer, hasta la dirección del GameServer (el plan de
// la CL4, CL4.3, paso 4), y con el GameServer, hasta el login (CL4.4, paso 4), sobre una
// Connection de verdad y un FakeSocket que hace de los dos servidores. Los bytes son los del 04
// (El flujo de conexión), escritos a mano; las direcciones, unas cualquiera, porque el FakeSocket
// no conecta. Lo cifrado lo descifra el GameServer de support/TestGameServer.h, con las claves
// sintéticas, y el login se compara con SameBytesWithoutValues: ningún mensaje trae sus bytes.
namespace
{
using Client::App::Credentials;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionFailure;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::App::TickCountMs;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;
using Client::Network::Connection;
using Client::Network::FoxAntiObfuscation;
using Client::Network::HeaderSize;
using Client::Network::IdentityObfuscation;
using Client::Network::IObfuscation;
using Client::Network::LinkState;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::SocketError;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::PlainBlockSize;
using Client::Protocol::ClientVersion;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::WriteLoginRequest;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameClientKeys;
using Client::Tests::GameServerEncryption;
using Client::Tests::GameServerReceived;
using Client::Tests::NoticeBytes;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

constexpr std::string_view ConnectHost = "10.0.0.1";
constexpr std::uint16_t ConnectPort = 44405;
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// Lo que mide el host en la respuesta F4 03 (04 § El flujo de conexión).
constexpr std::size_t AddressHostSize = 16;

// La semilla del relleno de lo que cifra la conexión: fija.
constexpr std::uint32_t PaddingSeed = 1;

// El código de servidor del 0 del laboratorio, como lo deja FoxAnti (el 0 más su delta, 400).
constexpr std::uint16_t LabServerCode = 400;

// La cuenta y la clave sintéticas de los fixtures del login (MuClientNetFixtures.h).
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

// Otra versión que la del cliente: la que manda el cliente del muproxy.log (BuildConstants.h).
constexpr std::string_view OtherVersion = "09908";

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

// El saludo del GameServer (F1 00): el resultado, el índice del jugador (5, en big-endian) y la
// versión, 5 bytes sin \0.
ByteVector GameHello(int result, std::string_view version)
{
    ByteVector bytes = Bytes({0xC1, 0x0C, 0xF1, 0x00, result, 0x00, 0x05});
    const std::span<const std::byte> text = BytesOf(version);
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

// La respuesta del login (F1 01).
ByteVector LoginResult(int result)
{
    return Bytes({0xC1, 0x05, 0xF1, 0x01, result});
}

// Un C3 de un bloque con un solo byte útil, el serial 0: pasa el checksum y no trae el opcode, así
// que no descifra con TooShort.
ByteVector OnlyTheSerial()
{
    const std::array<std::byte, PlainBlockSize> plain{};
    const auto block = EncryptBlock(GameServerEncryption(), plain, std::uint8_t{1});
    ByteVector bytes = Bytes({0xC3, 0x0D});
    bytes.insert(bytes.end(), block.begin(), block.end());
    return bytes;
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

SessionSettings Settings(Credentials credentials = Credentials{std::string(Account), std::string(Password)})
{
    return SessionSettings{{std::string(ConnectHost), ConnectPort}, std::move(credentials)};
}

// Una sesión con su conexión y lo que la rodea. La ofuscación es de quien la arma y vive más. La
// sesión abre el GameServer con las claves del cliente de TestGameServer.h.
struct Harness
{
    explicit Harness(const IObfuscation& obfuscation, SessionSettings settings = Settings())
        : connection(socket, obfuscation, clock, log, PaddingSeed),
          session(connection, clock, log, GameClientKeys(), std::move(settings))
    {
    }

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

    // Las líneas del log que dicen el host o el puerto de uno de los dos servidores: la dirección
    // es del servidor, y el log no la dice (ServerSession.cpp, OnServerAddress).
    [[nodiscard]] Lines LinesWithAnAddress() const
    {
        const std::string parts[] = {std::string(ConnectHost), std::string(GameHost), std::to_string(ConnectPort),
                                     std::to_string(GamePort)};
        Lines found;
        for (const std::string& line : sink.lines)
        {
            const auto says = [&line](const std::string& part) { return line.find(part) != std::string::npos; };
            if (std::ranges::any_of(parts, says))
            {
                found.push_back(line);
            }
        }
        return found;
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
        ASSERT_NO_FATAL_FAILURE(ReachServerList());
        socket.reads = {ServerList({LabServerCode})};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingServerAddress);
    }

    // Y la dirección: conecta al GameServer. Lo que sale desde acá es para el GS.
    void ReachGameServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachServerAddress());
        socket.reads = {AddressReply(GameHost, GamePort)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::ConnectingGameServer);
        gameServerStart = socket.sent.size();
    }

    // Y el Opened del GS: espera su saludo.
    void ReachGameHello()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameServer());
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingGameHello);
    }

    // Y el saludo, cifrado como lo manda el GS del laboratorio: el login salió.
    void ReachLoginResult()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameHello());
        socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), gameServerSends)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingLoginResult);
    }

    // Y el login aceptado.
    void ReachLoggedIn()
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoginResult());
        socket.reads = {FromTheGameServer(LoginResult(1), gameServerSends)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::LoggedIn);
    }

    // Lo que le llegó al GS desde que se abrió, como lo lee él: cada C3 descifrado, con sus
    // seriales desde 0.
    [[nodiscard]] GameServerReceived GameServerReads() const
    {
        PacketSerials serials;
        return Client::Tests::AsTheGameServerReceives(std::span(socket.sent).subspan(gameServerStart), serials);
    }

    // Si alguna línea del log trae la cuenta o la clave.
    [[nodiscard]] bool LoggedTheCredentials() const
    {
        return std::ranges::any_of(
            sink.lines, [](const std::string& line)
            { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; });
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    ServerSession session;
    std::size_t gameServerStart = 0; // dónde empieza en `socket.sent` lo que va al GS
    PacketSerials gameServerSends;   // los seriales de lo que cifra el GS
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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());

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

// Del ConnectServer al GameServer, el log no dice ningún host ni ningún puerto: ni el del CS, que
// es de la config, ni el del GS, que llegó del cable.
TEST(ServerSessionTest, TheLogDoesNotSayTheAddresses)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());

    EXPECT_FALSE(tested.sink.lines.empty());
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// El F4 03 y un saludo en la misma lectura: el saludo es del CS y la sesión deja de recorrer el
// lote. Si lo despachara, iría al dispatcher del GS, donde el 00 es el chat y no tiene handler, y
// habría un aviso. Cuando el GS abre, no sale nada: el saludo lo manda él.
TEST(ServerSessionTest, WhatFollowsTheAddressIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Join({AddressReply(GameHost, GamePort), Hello()})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);

    tested.session.Poll();
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Con el GameServer, el 00 va a su dispatcher, que tiene los F1 y no el 00: no es el saludo del CS
// ni pide otra lista.
TEST(ServerSessionTest, WithTheGameServerThe00IsNotTheConnectHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Hello()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] un paquete con el opcode 00 no tiene handler: se descarta, y este aviso no se repite"});
}

// Pregunta 9: una lista vacía es un error que lo dice, y la sesión cierra el CS.
TEST(ServerSessionTest, AnEmptyListFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {ServerList({})};
    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
}

// Un F4 03 antes de la lista no es la dirección: se ignora con un aviso, sin conectar a nada.
TEST(ServerSessionTest, AnUnexpectedPacketIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sent = tested.socket.sent;

    tested.socket.reads = {ServerList({401})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora la lista de servidores (F4 06): llegó fuera de turno"});
}

// La conexión con el ConnectServer no tiene claves: un C3 que llega es Undecryptable. La sesión no
// hace nada con él (el aviso es el de la conexión, uno solo) y lo que sigue en el lote se despacha.
TEST(ServerSessionTest, AnUndecryptablePacketIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Join({Bytes({0xC3, 0x05, 0x11, 0x22, 0x33}), ServerList({LabServerCode})})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01})})));
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un C3 que llegó: esta conexión no tiene claves"});
}

// Una lista que dice dos servidores y trae uno no se lee: se ignora con un aviso, y la sesión
// sigue esperando una que sirva.
TEST(ServerSessionTest, AListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Bytes({0xC1, 0x03, 0xF4})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un paquete mal formado del ConnectServer"});
}

TEST(ServerSessionTest, LosingTheConnectServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
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
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({})};
    tested.session.Poll();
    ASSERT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    tested.socket.reads = {Hello(), ServerList({LabServerCode}), AddressReply(GameHost, GamePort)};
    // El primer Poll lee las tres y abre el GS; el segundo da su Opened.
    tested.session.Poll();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

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
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// El GameServer del F4 03: la sesión conecta a ese, y cuando abre espera el saludo sin mandar nada,
// porque el GS saluda primero.
TEST(ServerSessionTest, ConnectsToTheGameServerItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    ASSERT_EQ(tested.socket.connects.size(), 2u);
    EXPECT_EQ(tested.socket.connects[1].host, GameHost);
    EXPECT_EQ(tested.socket.connects[1].port, GamePort);

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "conectó con el GameServer"));
}

// El test del 04: con C1 0C F1 00 01 00 05 "10209", el login sale cifrado como C3, con el serial
// 0, y el GS lo descifra a los 49 bytes de WriteLoginRequest con la cuenta, la clave y el tick
// del reloj en el momento del saludo.
TEST(ServerSessionTest, SendsTheLoginAfterTheHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.clock.Advance(std::chrono::milliseconds{123456});

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
    EXPECT_EQ(received.packets[0].Bytes().size(), 49u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), expected.Value().packet.Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se manda el login"));
}

// El GS del laboratorio manda el saludo cifrado: la conexión del GS tiene las claves y lo descifra.
TEST(ServerSessionTest, AHelloThatArrivesEncryptedAlsoSendsTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Un saludo con otro resultado que 1: HelloRejected, sin login, y la sesión cierra el GS. Se mira
// antes que la versión: con las dos cosas mal, también es HelloRejected.
TEST(ServerSessionTest, ARejectedHelloFails)
{
    for (const std::string_view version : {ClientVersion, OtherVersion})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

        tested.socket.reads = {GameHello(0, version)};
        tested.session.Poll();

        EXPECT_EQ(tested.session.State(), SessionState::Failed) << version;
        EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected) << version;
        EXPECT_EQ(tested.connection.State(), LinkState::Closed) << version;
        EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart) << version;
        EXPECT_TRUE(tested.Logged(LogLevel::Error, "el GameServer rechazó la conexión (resultado 0)")) << version;
    }
}

// Otra versión, con el saludo aceptado: VersionMismatch, sin login, con las dos versiones en el
// log.
TEST(ServerSessionTest, AnotherVersionFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {GameHello(1, OtherVersion)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("el GameServer tiene la versión {} y este cliente la {}",
                                                           OtherVersion, ClientVersion)));
}

// La versión viene del servidor: un byte que no se ve (un \0, un salto de línea, un DEL) sale como
// '?' en el log, que así sigue siendo de una línea.
TEST(ServerSessionTest, AVersionThatDoesNotPrintIsLoggedWithQuestionMarks)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    ByteVector hello = GameHello(1, ClientVersion);
    hello[8] = std::byte{0x00};
    hello[9] = std::byte{0x0A};
    hello[10] = std::byte{0x7F};

    tested.socket.reads = {hello};
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la versión 1???9 y este cliente la 10209"));
}

TEST(ServerSessionTest, AnAcceptedLoginIsLoggedIn)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer aceptó el login"));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Otro resultado: LoginRejected, con el resultado guardado (qué dice cada uno es de la CL5.5), y
// la sesión cierra el GS.
TEST(ServerSessionTest, ARejectedLoginFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::LoginRejected);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "el GameServer rechazó el login (resultado 0)"));
}

// Un C3 con el checksum malo: la sesión manda el F1 03, que el GS descifra a C1 06 F1 7B C1 73 con
// el serial 1 (el login gastó el 0), y sigue esperando el resultado. El que llega después, con el
// serial que sigue, se lee.
TEST(ServerSessionTest, ABadChecksumSendsTheReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    ByteVector bad = FromTheGameServer(LoginResult(1), tested.gameServerSends);
    const std::size_t firstChecksum = HeaderSize(PacketType::C3) + CipherBlockSize - 1;
    ASSERT_GT(bad.size(), firstChecksum);
    bad[firstChecksum] ^= std::byte{0xFF};

    tested.socket.reads = {bad};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[1].Bytes(), NoticeBytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se le avisa al GameServer (F1 03)"));

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Lo que no mide bloques enteros también lleva el aviso, como dice el contrato.
TEST(ServerSessionTest, APacketThatIsNotWholeBlocksSendsTheReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    tested.session.Poll();

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[1].Bytes(), NoticeBytes()));
}

// Con los otros errores, solo el aviso de la conexión: un C3 que pasa el checksum y no trae el
// opcode (TooShort) no manda el F1 03.
TEST(ServerSessionTest, AnotherUndecryptablePacketSendsNoReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.socket.reads = {OnlyTheSerial()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.Warnings().size(), 1u);
}

// Un saludo de más, ya mandado el login, no manda otro.
TEST(ServerSessionTest, AGameHelloOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora el saludo del GameServer (F1 00): llegó fuera de turno"});
}

// Un resultado antes del saludo no es el del login.
TEST(ServerSessionTest, ALoginResultOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {LoginResult(1)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se ignora el resultado del login (F1 01): llegó fuera de turno"});
}

// Un saludo que termina antes de la versión, y un resultado sin el resultado: no se leen, y la
// sesión sigue esperando.
TEST(ServerSessionTest, WhatDoesNotReadFromTheGameServerIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Bytes({0xC1, 0x07, 0xF1, 0x00, 0x01, 0x00, 0x05})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);

    tested.socket.reads = {GameHello(1, ClientVersion), Bytes({0xC1, 0x04, 0xF1, 0x01})};
    tested.session.Poll();
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora el saludo del GameServer (F1 00): no se puede leer",
                                        "[warning] se ignora el resultado del login (F1 01): no se puede leer"}));
}

// Una cuenta de 11 bytes no entra en el login (ReadCredentials ya no la deja pasar): la sesión no
// manda nada, falla con CredentialsTooLong y el error no trae la cuenta.
TEST(ServerSessionTest, CredentialsThatDoNotFitFail)
{
    const std::string tooLong = std::string(Account) + "xyz";
    Harness tested(IdentityObfuscation::Shared(), Settings(Credentials{tooLong, std::string(Password)}));
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::CredentialsTooLong);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "no se puede escribir el login"));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// El GS que cierra con el login aceptado: GameServerLost, con el motivo en el log.
TEST(ServerSessionTest, LosingTheGameServerAfterTheLoginFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
}

// Pregunta 7: de Start a LoggedIn, ninguna línea del log trae la cuenta ni la clave.
TEST(ServerSessionTest, TheLogDoesNotHaveTheCredentials)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    EXPECT_FALSE(tested.sink.lines.empty());
    EXPECT_FALSE(tested.LoggedTheCredentials());
    EXPECT_EQ(tested.LinesWithAnAddress(), Lines{});
}

// Start olvida el resultado del login, y el CS vuelve a abrir sin claves: un C3 ahí no manda el
// F1 03.
TEST(ServerSessionTest, StartingAgainForgetsTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    tested.session.Poll();
    const std::size_t sentBefore = tested.socket.sent.size();
    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    tested.session.Poll();
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
}
} // namespace
