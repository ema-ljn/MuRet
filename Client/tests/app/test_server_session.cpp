#include "App/ServerSession.h"

#include "App/CharacterEvent.h"
#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/GameLoop.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/Xor32.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/SocketError.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Character/CharacterCreate.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterSelect.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Keepalive.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Connection/ServerList.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"

#include "support/AppPrinters.h"
#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/IdleScene.h"
#include "support/FakeSocket.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Client::App::ServerSession con el ConnectServer, hasta la dirección del GameServer (el plan de
// la CL4, CL4.3, paso 4), y con el GameServer, hasta el login (CL4.4, paso 4), sobre una
// Connection de verdad y un FakeSocket que hace de los dos servidores. Los bytes son los del 04
// (El flujo de conexión), escritos a mano; las direcciones, unas cualquiera, porque el FakeSocket
// no conecta. Lo cifrado lo descifra el GameServer de support/TestGameServer.h, con las claves
// sintéticas, y el login se compara con SameBytesWithoutValues: ningún mensaje trae sus bytes.
// El keepalive y los topes de tiempo (CL4.5, paso 2) corren con el FakeClock: un "frame" es un
// Advance y un Poll.
namespace
{
using Client::App::CharacterCreated;
using Client::App::CharacterDeleted;
using Client::App::CharacterEvent;
using Client::App::CharacterListReceived;
using Client::App::Credentials;
using Client::App::GameLoop;
using Client::App::KeepaliveSpeeds;
using Client::App::PacingMode;
using Client::App::ReplyTimeout;
using Client::App::ServerAddress;
using Client::App::ServerSession;
using Client::App::SessionFailure;
using Client::App::SessionSettings;
using Client::App::SessionState;
using Client::App::TickCountMs;
using Client::App::WindowState;
using Client::Core::Duration;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Input::InputState;
using Client::Network::CloseReason;
using Client::Network::CloseReasonText;
using Client::Network::Connection;
using Client::Network::FoxAntiObfuscation;
using Client::Network::HeaderSize;
using Client::Network::IdentityObfuscation;
using Client::Network::IObfuscation;
using Client::Network::LinkState;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::SocketError;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::PlainBlockSize;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ClientVersion;
using Client::Protocol::Character::CharacterEntry;
using Client::Protocol::Character::CharacterOpcode;
using Client::Protocol::Character::CreateSubcode;
using Client::Protocol::Character::DeleteCodeSize;
using Client::Protocol::Character::DeleteSubcode;
using Client::Protocol::Character::ListSubcode;
using Client::Protocol::Character::SelectSubcode;
using Client::Protocol::Character::WriteCharacterCreate;
using Client::Protocol::Character::WriteCharacterDelete;
using Client::Protocol::Character::WriteCharacterSelect;
using Client::Protocol::Connection::Keepalive;
using Client::Protocol::Connection::KeepaliveAdjust20;
using Client::Protocol::Connection::KeepaliveOpcode;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::ServerEntry;
using Client::Protocol::Connection::WriteKeepalive;
using Client::Protocol::Connection::WriteLoginRequest;
using Client::Scenes::IScene;
using Client::Scenes::SceneId;
using Client::Scenes::SceneManager;
using Client::Tests::AddressReply;
using Client::Tests::BusyReply;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::CharacterEntryFields;
using Client::Tests::CharacterListBytes;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::FromTheGameServer;
using Client::Tests::GameClientKeys;
using Client::Tests::GameHello;
using Client::Tests::GameServerEncryption;
using Client::Tests::GameServerReceived;
using Client::Tests::Hello;
using Client::Tests::IdleScene;
using Client::Tests::Join;
using Client::Tests::ListRequest;
using Client::Tests::LoginResult;
using Client::Tests::LogoutResult;
using Client::Tests::NameBytes;
using Client::Tests::NoticeBytes;
using Client::Tests::PetInfoRequest;
using Client::Tests::RecordingFrameTarget;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::ServerList;

using std::chrono::milliseconds;
using std::chrono::seconds;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

constexpr std::string_view ConnectHost = "10.0.0.1";
constexpr std::uint16_t ConnectPort = 44405;
constexpr std::string_view GameHost = "10.0.0.2";
constexpr std::uint16_t GamePort = 55901;

// La semilla del relleno de lo que cifra la conexión: fija.
constexpr std::uint32_t PaddingSeed = 1;

// Lo que dura un frame de los tests del keepalive: un paso de la lógica, como un bucle a 25 Hz.
constexpr Duration Frame = milliseconds{40};

// Lo que dice el log de cada keepalive que sale.
constexpr std::string_view KeepaliveSent = "se mandó el keepalive";

// Dónde están las velocidades en el keepalive sin el XOR32: después de la cabecera, el opcode, el
// 00 y el tick (04 § El flujo de conexión).
constexpr std::size_t KeepalivePaddingIndex = 3;
constexpr std::size_t KeepaliveSpeedsIndex = 8;
constexpr std::size_t KeepaliveSpeedsSize = 4;

// El código de servidor del 0 del laboratorio, como lo deja FoxAnti (el 0 más su delta, 400).
constexpr std::uint16_t LabServerCode = 400;

// La cuenta y la clave sintéticas de los fixtures del login (MuClientNetFixtures.h).
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

// Otra versión que la del cliente: la que manda el cliente del muproxy.log (BuildConstants.h).
constexpr std::string_view OtherVersion = "09908";

// La lista del laboratorio como llega al cable, antes de que FoxAnti la pase a F4 06: un servidor.
ByteVector LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
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

SessionSettings Settings()
{
    return SessionSettings{{std::string(ConnectHost), ConnectPort}};
}

// La cuenta y la clave sintéticas, como las manda la ventana de la cuenta o el AutoLogin.
Credentials TheCredentials()
{
    return Credentials{std::string(Account), std::string(Password)};
}

// Dos personajes sintéticos, en los lugares 0 y 3, como los manda el GS en la lista (F3 00).
std::vector<CharacterEntryFields> TwoCharacters()
{
    return {{0, "char0001"}, {3, "char0002"}};
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

    // Y la lista, de un servidor: espera la elección.
    void ReachChoosingServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachServerList());
        socket.reads = {ServerList({LabServerCode})};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::ChoosingServer);
    }

    // Y la elección, el de la lista: espera la dirección.
    void ReachServerAddress()
    {
        ASSERT_NO_FATAL_FAILURE(ReachChoosingServer());
        session.ChooseServer(LabServerCode);
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

    // Y el saludo, cifrado como lo manda el GS del laboratorio: espera la cuenta y la clave.
    void ReachWaitingCredentials()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameHello());
        socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), gameServerSends)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::WaitingCredentials);
    }

    // Y la cuenta y la clave: el login salió.
    void ReachLoginResult()
    {
        ASSERT_NO_FATAL_FAILURE(ReachWaitingCredentials());
        session.SubmitCredentials(TheCredentials());
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

    // Y el pedido de la lista de personajes: espera la lista.
    void ReachWaitingCharacterList()
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoggedIn());
        session.RequestCharacterList();
        ASSERT_EQ(session.State(), SessionState::WaitingCharacterList);
    }

    // Y la lista, en claro como la manda el GS (04 § La selección de personaje): espera la elección.
    void ReachChoosingCharacter(const std::vector<CharacterEntryFields>& characters = TwoCharacters())
    {
        ASSERT_NO_FATAL_FAILURE(ReachWaitingCharacterList());
        socket.reads = {CharacterListBytes(characters)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::ChoosingCharacter);
    }

    // Los F3 de `subcode` que le llegaron al GS, sin el XOR32, en orden.
    [[nodiscard]] std::vector<Packet> CharacterPacketsSent(std::uint8_t subcode) const
    {
        std::vector<Packet> found;
        for (const Packet& packet : GameServerReads().packets)
        {
            Packet clear = Xor32Decode(packet);
            if (clear.Type() == PacketType::C1 && clear.Opcode() == CharacterOpcode && clear.Bytes().size() > 3 &&
                std::to_integer<std::uint8_t>(clear.Bytes()[3]) == subcode)
            {
                found.push_back(std::move(clear));
            }
        }
        return found;
    }

    // El login aceptado en el instante `time` del reloj, que empieza en 0.
    void LogInAt(Duration time)
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoginResult());
        AdvanceTo(time);
        socket.reads = {FromTheGameServer(LoginResult(1), gameServerSends)};
        session.Poll();
        ASSERT_EQ(session.State(), SessionState::LoggedIn);
    }

    void AdvanceTo(Duration time)
    {
        clock.Advance(time - clock.Now());
    }

    // Un Poll en cada Frame desde ahora hasta `time`, el de `time` incluido.
    void PollEachFrameUntil(Duration time)
    {
        while (clock.Now() + Frame <= time)
        {
            clock.Advance(Frame);
            session.Poll();
        }
    }

    // Los keepalives que le llegaron al GS, descifrados y sin el XOR32, en orden. Todo lo que le
    // llegó tiene que estar en orden, incluidos los keepalives previos al login, que también
    // consumen un serial de la conexión.
    [[nodiscard]] std::vector<Packet> Keepalives() const
    {
        const GameServerReceived received = GameServerReads();
        EXPECT_TRUE(
            std::ranges::all_of(received.serials, [](SerialCheck serial) { return serial == SerialCheck::InOrder; }));
        std::vector<Packet> keepalives;
        for (const Packet& packet : received.packets)
        {
            Packet clear = Xor32Decode(packet);
            if (clear.Opcode() == KeepaliveOpcode)
            {
                keepalives.push_back(std::move(clear));
            }
        }
        return keepalives;
    }

    // Cuántas líneas de `level` tienen `part`.
    [[nodiscard]] std::size_t CountLogged(LogLevel level, std::string_view part) const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
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

// Con la lista, la sesión espera la elección (CL5.5): ChoosingServer, con la lista en el orden en
// que llegó, y no sale ningún F4 03. La elección no tiene tope: el jugador puede tardar, y 60 s
// después sigue esperando sin mandar nada.
TEST(ServerSessionTest, WaitsForTheChoiceAfterTheList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {
        Client::Tests::ServerListWithLoads(std::vector<ServerEntry>{{401, 10}, {400, 20}, {402, 100}})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    const std::span<const ServerEntry> list = tested.session.ServerList();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].code, 401);
    EXPECT_EQ(list[0].load, 10);
    EXPECT_EQ(list[1].code, 400);
    EXPECT_EQ(list[2].code, 402);
    EXPECT_EQ(list[2].load, 100);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "la lista trae 3 servidores: se espera la elección"));
}

// La lista del laboratorio, con FoxAnti: la conexión la pasa a F4 06, ChooseServer(400) sale como
// C1 06 F4 03 00 00, porque FoxAnti lo pasa de vuelta al 0 del ConnectServer.
TEST(ServerSessionTest, ChoosingSendsTheCode)
{
    const FoxAntiObfuscation foxAnti;
    Harness tested(foxAnti);
    tested.session.Start();
    tested.socket.reads = {Hello()};
    tested.session.Poll();
    tested.socket.reads = {LabServerList()};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    ASSERT_EQ(tested.session.ServerList().size(), 1u);
    EXPECT_EQ(tested.session.ServerList()[0].code, LabServerCode);

    tested.session.ChooseServer(LabServerCode);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{LabServerCode});
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00})})));
}

// Cualquiera de la lista, no el primero: sin FoxAnti, el 402 sale como 92 01.
TEST(ServerSessionTest, ChoosingSendsTheCodeItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({401, 400, 402})};
    tested.session.Poll();

    tested.session.ChooseServer(402);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_TRUE(SameBytes(tested.socket.sent,
                          Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x06, 0xF4, 0x03, 0x92, 0x01})})));
}

// ChooseServer fuera de ChoosingServer: un aviso y nada sale, antes de la lista y con la elección
// ya mandada.
TEST(ServerSessionTest, ChoosingOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.session.ChooseServer(LabServerCode);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest()})));

    tested.socket.reads = {ServerList({LabServerCode, 401})};
    tested.session.Poll();
    tested.session.ChooseServer(LabServerCode);
    const ByteVector sent = tested.socket.sent;
    tested.session.ChooseServer(401);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{LabServerCode});
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora la elección de un servidor: la sesión no la espera",
                                        "[warning] se ignora la elección de un servidor: la sesión no la espera"}));
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

// El F4 05 es la respuesta del CS a un servidor ocupado, con el byte del servidor (el handler del
// original, 0x4c1bf0, no lo lee). Como el original, la sesión vuelve a pedir la lista con el
// mismo CS (Q-C71), y con la lista nueva espera otra elección.
TEST(ServerSessionTest, ABusyServerAsksForTheListAgain)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    const ByteVector sent = tested.socket.sent;

    tested.socket.reads = {BusyReply()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({sent, ListRequest()})));
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el servidor elegido está ocupado: se pide la lista otra vez"));

    tested.socket.reads = {ServerList({401})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    ASSERT_EQ(tested.session.ServerList().size(), 1u);
    EXPECT_EQ(tested.session.ServerList()[0].code, 401);
}

// Antes de elegir, un F4 05 no es la respuesta a nada: se ignora con un aviso (Q-C72).
TEST(ServerSessionTest, ABusyAnswerOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {BusyReply()};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.Warnings(),
              Lines{"[warning] se ignora el aviso de servidor ocupado (F4 05): llegó fuera de turno"});
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

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.ServerList().size(), 1u);
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

// Sin timeout, la espera de la elección no cuelga: un CS que cierra con la lista en pantalla es un
// ConnectServerLost.
TEST(ServerSessionTest, LosingTheConnectServerWhileChoosingFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());

    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Y la de la cuenta: un GS que cierra con la cuenta en pantalla es un GameServerLost.
TEST(ServerSessionTest, LosingTheGameServerWhileWaitingForTheCredentialsFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
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
    tested.socket.reads = {Hello(), ServerList({LabServerCode})};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    tested.session.ChooseServer(LabServerCode);
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    // El primer Poll lee la dirección y abre el GS; el segundo da su Opened.
    tested.session.Poll();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_TRUE(tested.session.ServerList().empty());
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
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

// Con el saludo del GS aceptado y la versión, la sesión espera la cuenta y la clave (CL5.5):
// sale el keepalive del timer global, pero no el login, y la espera humana no tiene tope.
// Después de 60 s sigue esperando credenciales y mandó los tres keepalives del original.
TEST(ServerSessionTest, WaitsForTheCredentialsAfterTheGameHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer saludó: se espera la cuenta y la clave"));
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// El test del 04: con C1 0C F1 00 01 00 05 "10209" y la cuenta y la clave, el login sale cifrado
// como C3, con el serial 0, y el GS lo descifra a los 49 bytes de WriteLoginRequest con la cuenta,
// la clave y el tick del reloj en el momento de SubmitCredentials.
TEST(ServerSessionTest, SubmittingSendsTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.clock.Advance(std::chrono::milliseconds{123456});

    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 1u);
    EXPECT_EQ(received.serials, std::vector<SerialCheck>{SerialCheck::InOrder});
    EXPECT_EQ(received.packets[0].Bytes().size(), 49u);
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[0].Bytes(), expected.Value().packet.Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se manda el login"));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// SubmitCredentials fuera de WaitingCredentials: un aviso, sin la cuenta ni la clave, y nada sale.
// Antes del saludo, y con el login ya mandado.
TEST(ServerSessionTest, SubmittingOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.session.SubmitCredentials(TheCredentials());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();
    tested.session.SubmitCredentials(TheCredentials());
    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignoran la cuenta y la clave: la sesión no las espera",
                                        "[warning] se ignoran la cuenta y la clave: la sesión no las espera"}));
    EXPECT_FALSE(tested.LoggedTheCredentials());
}

// El GS del laboratorio manda el saludo cifrado: la conexión del GS tiene las claves y lo descifra.
TEST(ServerSessionTest, AHelloThatArrivesEncryptedAlsoWaitsForTheCredentials)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());

    tested.socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
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

// Otro resultado: como el original, que vuelve al estado 2 con el GS abierto (0x643560), la sesión
// espera otra vez la cuenta y la clave, con el resultado guardado para el aviso de la escena. El
// login siguiente sale con el serial que sigue, y olvida el resultado de antes.
TEST(ServerSessionTest, ARejectedLoginKeepsItsResult)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el GameServer rechazó el login (resultado 0)"));
    tested.PollEachFrameUntil(seconds{60});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.Keepalives().size(), 3u);

    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 5u);
    EXPECT_TRUE(
        std::ranges::all_of(received.serials, [](SerialCheck serial) { return serial == SerialCheck::InOrder; }));
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

    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();
    tested.session.SubmitCredentials(TheCredentials());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF1, 0x01})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora el saludo del GameServer (F1 00): no se puede leer",
                                        "[warning] se ignora el resultado del login (F1 01): no se puede leer"}));
}

// Una cuenta de 11 bytes no entra en el login (ni ReadCredentials ni el TextField la dejan pasar):
// la sesión no manda nada, falla con CredentialsTooLong y el error no trae la cuenta.
TEST(ServerSessionTest, CredentialsThatDoNotFitFail)
{
    const std::string tooLong = std::string(Account) + "xyz";
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.session.SubmitCredentials(Credentials{tooLong, std::string(Password)});

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

// La orden de cerrar el juego (Q-C106): el F1 02 con 0, cifrado como lo manda el GS al final de su
// cuenta de 5 s. La sesión cierra la conexión y queda en QuitOrdered, sin falla y sin errores en
// el log: no es un GameServerLost, y main.cpp cierra el juego sin aviso.
TEST(ServerSessionTest, TheGameServersQuitOrderClosesWithoutAFailure)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer ordenó cerrar el juego (F1 02 con 0)"));
    EXPECT_EQ(tested.Warnings(), Lines{});
    EXPECT_EQ(tested.CountLogged(LogLevel::Error, ""), 0u);
}

// 0x4c2710 no mira la escena: la orden cierra también en la selección, y con ella se va la lista.
TEST(ServerSessionTest, TheQuitOrderAlsoClosesInTheSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
}

// El GS cierra la conexión justo después de la orden (CloseClient), y los dos pueden llegar en la
// misma lectura: el cierre es lo último del lote y no se recorre. Sigue sin falla.
TEST(ServerSessionTest, TheCloseThatFollowsTheQuitOrderIsNotALoss)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_FALSE(tested.Logged(LogLevel::Error, "se perdió la conexión"));
}

// 0x4c2710 no mira la escena ni si llegó cifrado: la orden en claro, con la cuenta en pantalla,
// también cierra.
TEST(ServerSessionTest, AQuitOrderInClearWhileWaitingForTheCredentialsAlsoCloses)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.socket.reads = {LogoutResult(0)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
}

// Con la orden, el keepalive se detiene, la sesión no se reconecta y no espera nada: en los 40 s
// siguientes no sale nada, no conecta y no hay un NoReply.
TEST(ServerSessionTest, TheQuitOrderStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::size_t sentBefore = tested.socket.sent.size();
    const std::size_t connectsBefore = tested.socket.connects.size();

    tested.PollEachFrameUntil(seconds{43});

    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
}

// Con 1 (la selección) y con 2 (la lista), el original cambia de escena (Q-C79): hasta la CL6 y la
// CL9, la sesión los ignora con un aviso y sigue con el login. Con otro, el original no hace nada,
// y acá, lo mismo con su aviso. Sin el resultado, no se puede leer.
TEST(ServerSessionTest, TheOtherLogoutResultsAreIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(1), tested.gameServerSends),
                           FromTheGameServer(LogoutResult(2), tested.gameServerSends),
                           FromTheGameServer(LogoutResult(3), tested.gameServerSends), Bytes({0xC1, 0x04, 0xF1, 0x02})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    const auto ignored = [](int result)
    {
        return std::format("[warning] se ignora la orden de salida del GameServer (F1 02 con {}): MuRet solo sigue la "
                           "de cerrar el juego, con 0",
                           result);
    };
    EXPECT_EQ(tested.Warnings(),
              (Lines{ignored(1), ignored(2), ignored(3),
                     "[warning] se ignora la orden de salida del GameServer (F1 02): no se puede leer"}));
}

// El ConnectServer no tiene F1: su dispatcher no tiene la orden (CL4.3), y un F1 02 con 0 de él no
// cierra nada. El original tiene un dispatcher solo y cerraría; un CS no lo manda.
TEST(ServerSessionTest, TheConnectServerCannotOrderTheQuit)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {LogoutResult(0)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
}

// QuitOrdered es el final: el juego se cierra en ese frame, y un Start de la escena (el Esc de la
// cuenta) no vuelve a conectar. Un aviso, y la sesión sigue igual.
TEST(ServerSessionTest, TheQuitOrderIsFinal)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::size_t connectsBefore = tested.socket.connects.size();
    const std::uint32_t startsBefore = tested.session.StartCount();

    tested.session.Start();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.StartCount(), startsBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] no se vuelve a conectar: el GameServer ordenó cerrar el juego"});
}

// Lo que llega detrás de la orden en la misma lectura no se despacha, como en el original, que al
// cerrar vacía su cola (0x4e8380): un saludo de más no deja el aviso de fuera de turno.
TEST(ServerSessionTest, WhatFollowsTheQuitOrderIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    const ByteVector order = FromTheGameServer(LogoutResult(0), tested.gameServerSends);
    const ByteVector hello = FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends);
    tested.socket.reads = {Join({order, hello})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.Warnings(), Lines{});
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
// El primero sale en el mismo Poll que lee el login aceptado, como el de 0x4ad100: un C3 con el
// serial 1 (el login gastó el 0) que el GS descifra a lo mismo que WriteKeepalive con el tick del
// reloj en ese momento y las velocidades en 0 y 0, sin ajuste.
TEST(ServerSessionTest, SendsTheFirstKeepaliveWithTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(milliseconds{12345}));

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    const auto expected = WriteKeepalive(Keepalive{TickCountMs(tested.clock.Now()), 0, 0});
    EXPECT_TRUE(SameBytes(received.packets[1].Bytes(), expected.packet.Bytes()));
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
}

// Sin SetKeepaliveSpeeds, el keepalive sin el XOR32 trae 0 y 0 en las dos velocidades y el 00
// después del opcode, como el primero de las 8 conexiones al GS (CL4.R, paso 6).
TEST(ServerSessionTest, WithoutACharacterTheSpeedsAreZero)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 1u);
    const std::span<const std::byte> bytes = keepalives[0].Bytes();
    ASSERT_EQ(bytes.size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_EQ(bytes[KeepalivePaddingIndex], std::byte{0});
    EXPECT_TRUE(SameBytes(bytes.subspan(KeepaliveSpeedsIndex), Bytes({0x00, 0x00, 0x00, 0x00})));
}

// El login en t = 3 s no reinicia el timer global: ninguno nuevo a los 19,999 s; a los 20 s,
// uno; hasta t = 100 s, 6 en total (3, 20, 40, 60, 80 y 100), con los seriales 1 a 6. LoggedIn no tiene tope:
// sin nada del GS en 100 s, la sesión sigue.
TEST(ServerSessionTest, KeepaliveEveryTwentySecondsAndNotBefore)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    tested.AdvanceTo(milliseconds{19999});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(seconds{20});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{100});

    EXPECT_EQ(tested.Keepalives().size(), 6u);
    EXPECT_EQ(tested.GameServerReads().serials.size(), 7u);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// El timer 1000 del original mira la conexión al GS, no el login (04 § El flujo de conexión).
// En 98 s salen cuatro keepalives: mientras se espera el saludo, las credenciales y el resultado,
// sin prolongar los límites de las respuestas del servidor.
TEST(ServerSessionTest, KeepaliveBeforeTheLogin)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.PollEachFrameUntil(seconds{29});
    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    tested.PollEachFrameUntil(seconds{69});
    tested.session.SubmitCredentials(TheCredentials());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingLoginResult);

    tested.PollEachFrameUntil(seconds{98});

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 5u);
    EXPECT_EQ(tested.Keepalives().size(), 4u);
}

// Un keepalive prelogin consume serial 0; el login consume 1 y su respuesta manda el 2.
// El timer siguiente sigue venciendo a los 40 s, no a los 45 del login aceptado.
TEST(ServerSessionTest, AcceptingTheLoginDoesNotRestartTheGlobalTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.PollEachFrameUntil(seconds{25});
    ASSERT_EQ(tested.Keepalives().size(), 1u);

    tested.session.SubmitCredentials(TheCredentials());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.session.Poll();
    ASSERT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(milliseconds{39999});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    tested.session.Poll();

    EXPECT_EQ(tested.Keepalives().size(), 3u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 4u);
}

// El timer corre sin conexión y con el CS: esos vencimientos no mandan ni quedan pendientes.
// El GS abierto a los 65 s espera al siguiente vencimiento global, a los 80 s.
TEST(ServerSessionTest, TheGlobalTimerConsumesTicksWithoutAGameServer)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.PollEachFrameUntil(seconds{20});
    EXPECT_TRUE(tested.socket.sent.empty());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const std::size_t sentToConnectServer = tested.socket.sent.size();
    tested.PollEachFrameUntil(seconds{65});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_EQ(tested.socket.sent.size(), sentToConnectServer);

    tested.session.ChooseServer(LabServerCode);
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.session.Poll();
    tested.gameServerStart = tested.socket.sent.size();
    tested.session.Poll();
    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(milliseconds{79999});
    tested.session.Poll();
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{80});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 1u);
}

// Cancelar/reconectar mantiene el timer, pero cada conexión vuelve su serial a 0.
TEST(ServerSessionTest, ReconnectingDoesNotRestartTheGlobalTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.PollEachFrameUntil(seconds{25});
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    tested.AdvanceTo(milliseconds{39999});
    tested.session.Poll();
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    EXPECT_EQ(tested.GameServerReads().serials, (std::vector<SerialCheck>{SerialCheck::InOrder}));
}

// El envío inmediato de F1 01 aceptado y el timer vencido son productores independientes.
TEST(ServerSessionTest, TheLoginAndTheDueTimerCanEachSendAKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{20}));
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);
    tested.AdvanceTo(milliseconds{39999});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// Un connect no bloqueante aún en curso no consume seriales ni encola el tick de los 20 s.
TEST(ServerSessionTest, AConnectingGameServerDoesNotQueueTheDueKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    tested.AdvanceTo(seconds{19});
    tested.socket.pollsUntilConnected = 2;
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    tested.session.Poll();
    tested.gameServerStart = tested.socket.sent.size();
    tested.AdvanceTo(seconds{20});
    tested.session.Poll();
    ASSERT_EQ(tested.connection.State(), LinkState::Connecting);
    tested.AdvanceTo(seconds{21});
    tested.session.Poll();
    tested.AdvanceTo(seconds{22});
    tested.socket.reads = {GameHello(1, ClientVersion)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 1u);
}

// Si el envío inmediato falla, no se intenta el periódico ni se procesa el resto del lote.
TEST(ServerSessionTest, AFailedImmediateKeepaliveDoesNotAttemptTheDueTimer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.AdvanceTo(seconds{20});
    tested.socket.sendError = SocketError::Closed;
    tested.socket.reads = {Join({LoginResult(1), LogoutResult(0)})};
    tested.session.Poll();

    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
    tested.session.Poll();
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// Un frame de 60 s (un breakpoint) con el login en t = 3 s: en t = 63 s sale uno solo, con el
// serial 2, y el siguiente a los 83 s, sin recuperar los vencimientos de 20, 40 y 60 s.
TEST(ServerSessionTest, ASixtySecondFrameSendsOneKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    tested.AdvanceTo(seconds{63});
    tested.session.Poll();
    tested.session.Poll();
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(milliseconds{82960});
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{83});
    EXPECT_EQ(tested.Keepalives().size(), 3u);
}

// Con el GameLoop y la ventana minimizada, el Poll sigue (CL4.1) y con él el keepalive: 100 s
// después del login, el del login y cinco más. Main pone la sesión en el Poll del SceneManager,
// como acá, con una escena que no hace nada en el lugar de la selección; no se dibuja nada.
TEST(ServerSessionTest, KeepsTheKeepaliveWhileMinimized)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    RecordingFrameTarget frame;
    SceneManager scenes([](SceneId) -> std::unique_ptr<IScene> { return std::make_unique<IdleScene>(); }, frame,
                        tested.clock, [&tested] { tested.session.Poll(); }, tested.log);
    ASSERT_TRUE(scenes.Start(SceneId::CharacterSelect).HasValue());
    GameLoop loop{tested.clock, scenes, PacingMode::Decoupled};
    WindowState minimized;
    minimized.minimized = true;
    InputState input;
    const Duration end = tested.clock.Now() + seconds{100};

    while (tested.clock.Now() <= end)
    {
        loop.RunFrame(minimized, input);
    }

    EXPECT_EQ(tested.Keepalives().size(), 6u);
    EXPECT_GT(loop.Stats().frames, 0u);
    EXPECT_TRUE(frame.frames.empty());
}

// Las velocidades que dio SetKeepaliveSpeeds, con el ajuste de KeepaliveSpeed: 0x0328 + 20 y
// 0x0268 + 20 con KeepaliveAdjust20 salen como 28 03 68 02. El XOR32 sigue puesto después de
// descifrar: se mira sin él.
TEST(ServerSessionTest, UsesTheSpeedsItWasGiven)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.SetKeepaliveSpeeds(KeepaliveSpeeds{.attackSpeed = std::uint16_t{0x0328 + 20},
                                                      .magicSpeed = std::uint16_t{0x0268 + 20},
                                                      .characterFlags = KeepaliveAdjust20});
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 1u);
    const std::span<const std::byte> bytes = keepalives[0].Bytes();
    ASSERT_EQ(bytes.size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_TRUE(SameBytes(bytes.subspan(KeepaliveSpeedsIndex), Bytes({0x28, 0x03, 0x68, 0x02})));
}

// Las velocidades que llegan con el login ya aceptado (la CL6 las da al elegir el personaje) van en
// el keepalive siguiente: el del login sale con 0 y 0, y el de los 20 s con las nuevas.
TEST(ServerSessionTest, SpeedsGivenAfterTheLoginGoInTheNextKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.session.SetKeepaliveSpeeds(KeepaliveSpeeds{.attackSpeed = std::uint16_t{0x0328 + 20},
                                                      .magicSpeed = std::uint16_t{0x0268 + 20},
                                                      .characterFlags = KeepaliveAdjust20});

    tested.AdvanceTo(seconds{23});
    tested.session.Poll();

    const std::vector<Packet> keepalives = tested.Keepalives();
    ASSERT_EQ(keepalives.size(), 2u);
    ASSERT_EQ(keepalives[0].Bytes().size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    ASSERT_EQ(keepalives[1].Bytes().size(), KeepaliveSpeedsIndex + KeepaliveSpeedsSize);
    EXPECT_TRUE(SameBytes(keepalives[0].Bytes().subspan(KeepaliveSpeedsIndex), Bytes({0x00, 0x00, 0x00, 0x00})));
    EXPECT_TRUE(SameBytes(keepalives[1].Bytes().subspan(KeepaliveSpeedsIndex), Bytes({0x28, 0x03, 0x68, 0x02})));
}

// El GS que corta con el login aceptado: GameServerLost con PeerClosed en el log, el keepalive se
// detiene y la sesión no se reconecta sola. En los 40 s siguientes no sale nada.
TEST(ServerSessionTest, LosingTheGameServerStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.socket.peerClosed = true;
    tested.session.Poll();
    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, std::format("se perdió la conexión con el GameServer: {}",
                                                           CloseReasonText(CloseReason::PeerClosed))));
    const std::size_t sentBefore = tested.socket.sent.size();
    const std::size_t connectsBefore = tested.socket.connects.size();

    tested.PollEachFrameUntil(seconds{43});

    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.socket.connects.size(), connectsBefore);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
}

// Start desde LoggedIn vuelve al CS: el timer sigue, pero no manda keepalive al CS, solo el A9.
TEST(ServerSessionTest, StartingAgainStopsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    tested.session.Start();
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.PollEachFrameUntil(seconds{25});

    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(std::span(tested.socket.sent).subspan(sentBefore), PetInfoRequest()));
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, KeepaliveSent), 1u);
}

// Un CS que acepta y no saluda: a los ReplyTimeout del A9 la sesión cierra y falla con NoReply, y
// el log dice qué servidor y qué se esperaba. Un ms antes, sigue esperando.
TEST(ServerSessionTest, AConnectServerThatDoesNotGreetFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.AdvanceTo(ReplyTimeout - milliseconds{1});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    tested.AdvanceTo(ReplyTimeout);
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "el ConnectServer no contestó en 30 s: se esperaba su saludo"));
}

// Cada espera cuenta desde que empezó: con el saludo a los 20 s, la lista se espera hasta los 50 s.
TEST(ServerSessionTest, EachWaitCountsFromItsStart)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    tested.session.Poll();
    tested.AdvanceTo(seconds{20});
    tested.socket.reads = {Hello()};
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerList);

    tested.AdvanceTo(milliseconds{49999});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    tested.AdvanceTo(seconds{50});
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el ConnectServer no contestó en 30 s: se esperaba la lista de servidores"));
}

// El GS que no contesta el login también es NoReply, con el GS en el log.
TEST(ServerSessionTest, AGameServerThatDoesNotAnswerTheLoginFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.AdvanceTo(ReplyTimeout);
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba el resultado del login"));
}

// Un CS que da la lista y no la dirección: la espera de la dirección cuenta desde la elección (a
// los 10 s), así que falla a los 40 s y no a los 30.
TEST(ServerSessionTest, AConnectServerThatDoesNotGiveTheAddressFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    tested.AdvanceTo(seconds{10});
    tested.session.ChooseServer(LabServerCode);
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerAddress);

    tested.AdvanceTo(seconds{40} - milliseconds{1});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    tested.AdvanceTo(seconds{40});
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error,
                              "el ConnectServer no contestó en 30 s: se esperaba la dirección del GameServer"));
}

// Un GS que acepta y no saluda: la espera cuenta desde que se abrió (a los 5 s) y falla a los 35 s,
// con el GS en el log.
TEST(ServerSessionTest, AGameServerThatDoesNotGreetFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    tested.AdvanceTo(seconds{5});
    tested.session.Poll();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

    tested.AdvanceTo(seconds{35} - milliseconds{1});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    tested.AdvanceTo(seconds{35});
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba su saludo"));
}

// Lo que llegó durante un frame largo se lee antes de mirar el tope: un resultado que estaba en el
// socket después de 60 s sin frames entra, y la sesión no falla.
TEST(ServerSessionTest, AReplyThatArrivedDuringALongFrameIsRead)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.AdvanceTo(seconds{60});
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
}

// Main loguea cada cambio de estado (el plan de la CL4, CL4.5): de Start a LoggedIn, una línea de
// info por estado, en orden, y después la del primer keepalive. La del cierre del CS es de la
// Connection. La elección no dice el código, y el login no dice la cuenta.
TEST(ServerSessionTest, EachStateChangeIsLogged)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    Lines infos;
    for (std::size_t index = 0; index < tested.sink.lines.size(); ++index)
    {
        if (tested.sink.levels[index] == LogLevel::Info)
        {
            infos.push_back(tested.sink.lines[index]);
        }
    }
    EXPECT_EQ(infos, (Lines{"[info] se conecta al ConnectServer", "[info] conectó con el ConnectServer",
                            "[info] el ConnectServer saludó: se pide la lista de servidores",
                            "[info] la lista trae 1 servidores: se espera la elección",
                            "[info] se elige un servidor: se pide su dirección",
                            "[info] el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese",
                            "[info] la conexión se cerró: la cerró el cliente",
                            "[info] conectó con el GameServer: se espera su saludo",
                            "[info] el GameServer saludó: se espera la cuenta y la clave", "[info] se manda el login",
                            "[info] el GameServer aceptó el login", "[info] se mandó el keepalive al GameServer"}));
}

// La lista de personajes y la entrada al mundo (el plan de la CL6, CL6.3, paso 2). Los nombres son
// sintéticos (support/CharacterPackets.h).

// En LoggedIn, el F3 00 sale en claro con su XOR32, como lo manda 0x634e0e, y la sesión espera la
// lista (el estado 0x32). Al GS le llegaron dos cifrados, el login y el primer keepalive: el F3 00
// no fue uno.
TEST(ServerSessionTest, RequestCharacterListSendsF300)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.RequestCharacterList();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<Packet> requests = tested.CharacterPacketsSent(ListSubcode);
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_TRUE(SameBytes(requests[0].Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
    EXPECT_EQ(tested.GameServerReads().serials.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide la lista de personajes"));
}

TEST(ServerSessionTest, RequestingTheCharacterListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.session.RequestCharacterList();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Warning, "se ignora el pedido de la lista de personajes: la sesión no lo espera"));
}

// La lista llega en claro (04): la sesión la guarda en el orden del paquete y espera la elección
// (el 0x33), sin ninguno elegido.
TEST(ServerSessionTest, ADuplicatedListSlotChoosesTheLastConstructedEntryAndKeepsBothEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter({{0, "char0001"}, {0, "char0002"}}));
    tested.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0002"));
    const auto events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto& list = std::get<CharacterListReceived>(events[0]).list.characters;
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].name, NameBytes("char0001"));
    EXPECT_EQ(list[1].name, NameBytes("char0002"));
}

TEST(ServerSessionTest, TheListMovesToChoosingCharacter)
{
    Harness tested(IdentityObfuscation::Shared());

    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    ASSERT_TRUE(tested.session.Characters().has_value());
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 2u);
    EXPECT_EQ(characters[0].slot, 0);
    EXPECT_EQ(characters[1].slot, 3);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "la lista trae 2 personajes: se espera la elección"));
}

TEST(ServerSessionTest, CharacterEventsStartEmpty)
{
    Harness tested(IdentityObfuscation::Shared());

    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

// El lote posee cada campo, incluido el orden y los slots repetidos que admite el lector.
TEST(ServerSessionTest, TakingTheListEventPreservesItsPayloadAndConsumesItOnce)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    const std::vector<CharacterEntryFields> entries{
        {3, "char0002", 513, 0x81, 0x21, {0x12, 0x34, 0x56}, 0x72}, {0, "char0001"}, {3, "char0003"}};
    tested.socket.reads = {CharacterListBytes(entries, 3, 5, 7)};

    tested.session.Poll();
    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();

    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->list.creatableClasses, 5);
    EXPECT_EQ(received->list.notice, 7);
    ASSERT_EQ(received->list.characters.size(), 3u);
    const CharacterEntry& entry = received->list.characters[0];
    EXPECT_EQ(entry.slot, 3);
    EXPECT_EQ(entry.name, NameBytes("char0002"));
    EXPECT_EQ(entry.level, 513);
    EXPECT_EQ(entry.flags, 0x81);
    EXPECT_EQ(entry.classByte, 0x21);
    const std::array<std::byte, Client::Protocol::Character::EquipmentSize> equipment{std::byte{0x12}, std::byte{0x34},
                                                                                      std::byte{0x56}};
    EXPECT_EQ(entry.equipment, equipment);
    EXPECT_EQ(entry.guildStatus, 0x72);
    EXPECT_FALSE(entry.withoutEquipment);
    EXPECT_EQ(received->list.characters[1].slot, 0);
    EXPECT_EQ(received->list.characters[2].slot, 3);
    EXPECT_EQ(received->list.characters[2].name, NameBytes("char0003"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
    ASSERT_TRUE(tested.session.Characters().has_value());
    // El evento retiene tres construcciones; la vista UI conserva sólo el último de cada slot.
    const auto& visible = tested.session.Characters()->characters;
    ASSERT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0].slot, 0);
    EXPECT_EQ(visible[0].name, NameBytes("char0001"));
    EXPECT_EQ(visible[1].slot, 3);
    EXPECT_EQ(visible[1].name, NameBytes("char0003"));
}

// Con la cantidad en 0 el original pasa igual al 0x33: la selección, sin nadie para elegir.
TEST(ServerSessionTest, AnEmptyListAlsoChooses)
{
    Harness tested(IdentityObfuscation::Shared());

    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter({}));

    ASSERT_TRUE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.session.Characters()->characters.empty());
    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_TRUE(received->list.characters.empty());
}

TEST(ServerSessionTest, ACharacterListOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): llegó fuera de turno"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ACharacterListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());

    tested.socket.reads = {CharacterListBytes({{}}, 2)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): no se puede leer"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

// Elegir es del clic (0x6342c0): guarda el elegido y no manda nada.
TEST(ServerSessionTest, ChooseCharacterSendsNothing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    const std::size_t sentBefore = tested.socket.sent.size();

    tested.session.ChooseCharacter(std::uint8_t{3});

    const CharacterEntry* chosen = tested.session.ChosenCharacter();
    ASSERT_NE(chosen, nullptr);
    EXPECT_EQ(chosen->slot, 3);
    EXPECT_EQ(chosen->name, NameBytes("char0002"));
    EXPECT_EQ(tested.socket.sent.size(), sentBefore);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
}

// Un clic sobre nadie (std::nullopt, el -1 de 0x6342c0) o un lugar que no está en la lista dejan
// sin elegido.
TEST(ServerSessionTest, ChoosingNothingForgetsTheChoice)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.ChooseCharacter(std::uint8_t{3});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::nullopt);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::uint8_t{0});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    tested.session.ChooseCharacter(std::uint8_t{2});
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
}

TEST(ServerSessionTest, ChoosingACharacterOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());

    tested.session.ChooseCharacter(std::uint8_t{0});

    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la elección de un personaje: la sesión no la espera"));
}

// Con uno elegido, el F3 03 sale en claro con su nombre como vino en la lista, y la sesión pasa al
// 0x3c. El log dice el lugar y no el nombre.
TEST(ServerSessionTest, EnterWorldSendsF303WithTheChosenName)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    const std::vector<Packet> selects = tested.CharacterPacketsSent(SelectSubcode);
    ASSERT_EQ(selects.size(), 1u);
    EXPECT_TRUE(SameBytes(selects[0].Bytes(), Xor32Decode(WriteCharacterSelect(NameBytes("char0002")).packet).Bytes()));
    EXPECT_EQ(tested.GameServerReads().serials.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se entra al mundo con el personaje del lugar 3"));
}

TEST(ServerSessionTest, EnterWorldWithoutAChoiceWarns)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(tested.CharacterPacketsSent(SelectSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se entra al mundo: no hay un personaje elegido"));
}

TEST(ServerSessionTest, EnteringTheWorldOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.CharacterPacketsSent(SelectSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la entrada al mundo: la sesión no la espera"));
}

// El keepalive sigue en los tres estados nuevos con el timer global (a los 20, 40 y 60 s):
// cambiar de estado no lo vuelve a empezar. Si empezara con el pedido, a los 10 s, a los 22 s
// habría uno solo.
TEST(ServerSessionTest, TheKeepaliveRunsInTheThreeNewStates)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{1}));

    tested.PollEachFrameUntil(seconds{10});
    tested.session.RequestCharacterList();
    tested.PollEachFrameUntil(seconds{22});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.PollEachFrameUntil(seconds{30});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.EnterWorld();
    tested.PollEachFrameUntil(seconds{42});
    EXPECT_EQ(tested.Keepalives().size(), 3u);
    tested.PollEachFrameUntil(seconds{62});

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.Keepalives().size(), 4u);
}

// Un código de borrar sintético, de 10 bytes.
std::array<std::byte, DeleteCodeSize> SyntheticDeleteCode()
{
    std::array<std::byte, DeleteCodeSize> code{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        code[k] = static_cast<std::byte>('1' + k);
    }
    return code;
}

// La respuesta de crear con 1 (0x4c2290): el nombre, el lugar, el nivel y la clase.
std::vector<std::byte> CreatedReply(std::uint8_t slot, std::uint8_t classByte = 0x20)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x13, 0xF3, 0x01, 0x01});
    const auto name = NameBytes("char0003");
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.push_back(std::byte{slot});
    bytes.push_back(std::byte{0x01});
    bytes.push_back(std::byte{0x00});
    bytes.push_back(std::byte{classByte});
    return bytes;
}

// La fotografía final ya no contiene al borrado; el lote conserva lista, alta normalizada y baja.
TEST(ServerSessionTest, CharacterEventsKeepTheirOrderAndOwnTheirPayloads)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    tested.session.Poll();
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.session.ChooseCharacter(std::uint8_t{0}); // fuera de turno: no cambia el lugar enviado
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};
    tested.session.Poll();
    tested.session.ChooseCharacter(std::uint8_t{0});

    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();

    ASSERT_EQ(events.size(), 3u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    const auto* created = std::get_if<CharacterCreated>(&events[1]);
    const auto* deleted = std::get_if<CharacterDeleted>(&events[2]);
    ASSERT_NE(received, nullptr);
    ASSERT_NE(created, nullptr);
    ASSERT_NE(deleted, nullptr);
    ASSERT_EQ(received->list.characters.size(), 2u);
    EXPECT_EQ(received->list.characters[1].slot, 3);
    EXPECT_EQ(received->list.characters[1].name, NameBytes("char0002"));
    EXPECT_EQ(created->entry.slot, 3);
    EXPECT_EQ(created->entry.name, NameBytes("char0003"));
    EXPECT_EQ(created->entry.level, 1);
    EXPECT_EQ(created->entry.flags, 0);
    EXPECT_EQ(created->entry.classByte, 0x20);
    const std::array<std::byte, Client::Protocol::Character::EquipmentSize> noEquipment{};
    EXPECT_EQ(created->entry.equipment, noEquipment);
    EXPECT_TRUE(created->entry.withoutEquipment);
    EXPECT_EQ(created->entry.guildStatus, 0xff);
    EXPECT_EQ(deleted->slot, 3);
    EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.ChosenCharacter()->slot, 0);
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());

    tested.session.Start();

    EXPECT_EQ(received->list.characters[1].name, NameBytes("char0002"));
    EXPECT_EQ(created->entry.name, NameBytes("char0003"));
    EXPECT_EQ(deleted->slot, 3);
}

TEST(ServerSessionTest, TwoAcceptedIdenticalCreationsPublishTwoEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    ASSERT_EQ(tested.session.TakeCharacterEvents().size(), 1u);

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {CreatedReply(1)};
        tested.session.Poll();
        ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    }

    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 2u);
    for (const CharacterEvent& event : events)
    {
        const auto* created = std::get_if<CharacterCreated>(&event);
        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->entry.slot, 1);
        EXPECT_EQ(created->entry.name, NameBytes("char0003"));
        EXPECT_TRUE(created->entry.withoutEquipment);
    }
    EXPECT_EQ(tested.session.Characters()->characters.size(), 3u);
}

TEST(ServerSessionTest, AnAcceptedListSurvivesACloseInTheSamePoll)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.socket.peerClosed = true;

    tested.session.Poll();

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* received = std::get_if<CharacterListReceived>(&events[0]);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->list.characters.size(), 2u);
    ASSERT_TRUE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
}

// F1/02 con 0 invalida el resto del lote; el alta aceptada antes permanece disponible.
TEST(ServerSessionTest, AcceptedEventsSurviveQuitAndLaterPacketsStayIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    ASSERT_EQ(tested.session.TakeCharacterEvents().size(), 1u);
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {
        Join({CreatedReply(1), FromTheGameServer(LogoutResult(0), tested.gameServerSends), CreatedReply(2)})};

    tested.session.Poll();

    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* created = std::get_if<CharacterCreated>(&events[0]);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->entry.slot, 1);
}

TEST(ServerSessionTest, StartDiscardsPendingCharacterEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.Start();

    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
    EXPECT_FALSE(tested.session.Characters().has_value());
}

// En ChoosingCharacter, CreateCharacter manda el F3 01 con el nombre y la clase y espera su respuesta
// (0x34). El log dice la clase y no el nombre.
TEST(ServerSessionTest, CreateCharacterSendsF301WithTheNameAndTheClass)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.session.CreateCharacter("char0003", 1);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    const std::vector<Packet> creates = tested.CharacterPacketsSent(CreateSubcode);
    ASSERT_EQ(creates.size(), 1u);
    EXPECT_TRUE(
        SameBytes(creates[0].Bytes(), Xor32Decode(WriteCharacterCreate(NameBytes("char0003"), 1).packet).Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide crear un personaje de la clase 1"));
    EXPECT_FALSE(tested.Logged(LogLevel::Info, "char0003"));
}

// Un nombre que no entra en el paquete no sale, y crear fuera de ChoosingCharacter tampoco.
TEST(ServerSessionTest, CreatingOutOfTurnOrWithALongNameIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.session.CreateCharacter("char0003", 1);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora el pedido de crear un personaje: la sesión no lo espera"));

    Harness choosing(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(choosing.ReachChoosingCharacter());
    choosing.session.CreateCharacter("abcdefghijk", 1);
    EXPECT_EQ(choosing.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(choosing.CharacterPacketsSent(CreateSubcode).empty());
    EXPECT_TRUE(choosing.Logged(LogLevel::Warning, "no se crea el personaje: el nombre no entra en 10 bytes"));
}

// Con 1, el nuevo va a su lugar, sin equipo (0x517270, Q-C103), y la sesión vuelve a la elección.
TEST(ServerSessionTest, TheNewOneGoesToItsSlotWithoutEquipment)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);

    tested.socket.reads = {CreatedReply(1)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 3u);
    EXPECT_EQ(characters[2].slot, 1);
    EXPECT_EQ(characters[2].name, NameBytes("char0003"));
    EXPECT_EQ(characters[2].level, 1);
    EXPECT_EQ(characters[2].classByte, 0x20);
    EXPECT_TRUE(characters[2].withoutEquipment);
    EXPECT_FALSE(characters[0].withoutEquipment);
    // Sin gremio: 0x511290 pone +0x368 en 0xff (0x511ca6) y la respuesta no lo toca.
    EXPECT_EQ(characters[2].guildStatus, 0xff);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se creó el personaje del lugar 1"));
}

// Uno que llega a un lugar ocupado lo pisa (0x4c2290 escribe chars[lugar]); uno de un lugar de más de 4
// no se suma: el original no le da posición.
TEST(ServerSessionTest, TheNewOneReplacesItsSlotAndOneBeyondFourIsDropped)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_EQ(tested.session.Characters()->characters[1].name, NameBytes("char0003"));
    ASSERT_EQ(tested.session.TakeCharacterEvents().size(), 2u);

    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(5)};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el personaje creado no se suma: el lugar 5 no está en la selección"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

// Con 0 o 2, solo el resultado, para el aviso de la ventana; con otro, el original no hace nada y se
// queda esperando (Q-C114).
TEST(ServerSessionTest, ARefusedCreationKeepsItsResult)
{
    for (const int result : {0, 2})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        static_cast<void>(tested.session.TakeCharacterEvents());
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, static_cast<unsigned char>(result)})};
        tested.session.Poll();
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
    }
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x05})};
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el resultado 5 no lo atiende el original"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

// La respuesta de crear fuera de turno se ignora.
TEST(ServerSessionTest, ACreateReplyOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());

    tested.socket.reads = {CreatedReply(1)};
    tested.session.Poll();

    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Warning, "se ignora la respuesta de crear un personaje (F3 01): llegó fuera de turno"));
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, AMalformedCreationPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x01})};

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ASecondCreateReplyWithoutANewRequestPublishesNoEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Join({CreatedReply(1), CreatedReply(1)})};

    tested.session.Poll();

    const std::vector<CharacterEvent> events = tested.session.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<CharacterCreated>(events[0]));
}

// DeleteCharacter manda el F3 02 con el nombre del elegido y el código, y espera (0x38). El log dice
// el lugar, y ni el nombre ni el código.
TEST(ServerSessionTest, DeleteCharacterSendsF302WithTheChosenNameAndTheCode)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});

    tested.session.DeleteCharacter(SyntheticDeleteCode());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    const std::vector<Packet> deletes = tested.CharacterPacketsSent(DeleteSubcode);
    ASSERT_EQ(deletes.size(), 1u);
    EXPECT_TRUE(
        SameBytes(deletes[0].Bytes(),
                  Xor32Decode(WriteCharacterDelete(NameBytes("char0002"), SyntheticDeleteCode()).packet).Bytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se pide borrar el personaje del lugar 3"));
    EXPECT_FALSE(tested.Logged(LogLevel::Info, "1234"));
}

// Sin elegido, o fuera de ChoosingCharacter, no sale nada.
TEST(ServerSessionTest, DeletingWithoutAChoiceOrOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_TRUE(tested.CharacterPacketsSent(DeleteSubcode).empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se borra un personaje: no hay uno elegido"));

    Harness logged(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(logged.ReachLoggedIn());
    logged.session.DeleteCharacter(SyntheticDeleteCode());
    EXPECT_TRUE(logged.Logged(LogLevel::Warning, "se ignora el pedido de borrar un personaje: la sesión no lo espera"));
}

// Con 1 se saca al que se mandó, y queda sin elegido: uno nuevo en el mismo lugar no sale elegido. El
// original borra al elegido al llegar la respuesta (Q-C104); en MuRet no puede cambiar mientras se
// espera, porque ChooseCharacter es de ChoosingCharacter.
TEST(ServerSessionTest, DeleteRemovesTheSlotItSent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());

    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};
    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 1u);
    EXPECT_EQ(characters[0].slot, 0);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se borró el personaje del lugar 3"));
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    tested.session.Poll();
    ASSERT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
}

// Start olvida los dos resultados, como la lista.
TEST(ServerSessionTest, StartForgetsTheResults)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    tested.session.Poll();
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x00})};
    tested.session.Poll();
    ASSERT_TRUE(tested.session.CreateResult().has_value());
    ASSERT_TRUE(tested.session.DeleteResult().has_value());

    tested.session.Start();

    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_EQ(tested.session.DeleteResult(), std::nullopt);
}

// Con otro resultado no se saca a nadie, y el elegido sigue: el aviso es de la ventana.
TEST(ServerSessionTest, ARefusedDeletionKeepsTheCharacter)
{
    for (const int result : {0, 3, 9})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        static_cast<void>(tested.session.TakeCharacterEvents());
        tested.session.ChooseCharacter(std::uint8_t{3});
        tested.session.DeleteCharacter(SyntheticDeleteCode());
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, static_cast<unsigned char>(result)})};
        tested.session.Poll();
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_NE(tested.session.ChosenCharacter(), nullptr);
        EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
    }
}

TEST(ServerSessionTest, AMalformedDeletionPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF3, 0x02})};

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    EXPECT_EQ(tested.session.DeleteResult(), std::nullopt);
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ADeleteReplyOutOfTurnPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.session.TakeCharacterEvents());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};

    tested.session.Poll();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.session.TakeCharacterEvents().empty());
}

// Las dos respuestas se esperan con el tope de los Waiting*, y el keepalive sigue en los dos estados.
TEST(ServerSessionTest, CreatingAndDeletingHaveTheReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.AdvanceTo(seconds{5});
    tested.session.CreateCharacter("char0003", 1);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    tested.session.Poll();
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(tested.Logged(LogLevel::Error,
                              "el GameServer no contestó en 30 s: se esperaba el resultado de crear un personaje"));

    Harness deleting(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(deleting.ReachChoosingCharacter());
    deleting.session.ChooseCharacter(std::uint8_t{0});
    deleting.AdvanceTo(seconds{5});
    deleting.session.DeleteCharacter(SyntheticDeleteCode());
    deleting.AdvanceTo(seconds{5} + ReplyTimeout);
    deleting.session.Poll();
    EXPECT_TRUE(deleting.Logged(LogLevel::Error,
                                "el GameServer no contestó en 30 s: se esperaba el resultado de borrar un personaje"));
}

TEST(ServerSessionTest, TheKeepaliveRunsWhileCreatingAndDeleting)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{1}));
    tested.session.RequestCharacterList();
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    tested.PollEachFrameUntil(seconds{2});
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    const std::size_t before = tested.Keepalives().size();

    tested.session.CreateCharacter("char0003", 1);
    tested.PollEachFrameUntil(seconds{22});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.Keepalives().size(), before + 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x00})};
    tested.PollEachFrameUntil(seconds{23});
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.PollEachFrameUntil(seconds{42});
    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    EXPECT_EQ(tested.Keepalives().size(), before + 2);
}

// La lista se espera con el tope de los Waiting*, desde el pedido.
TEST(ServerSessionTest, WaitingCharacterListHasTheReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();

    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.session.Poll();
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Error, "el GameServer no contestó en 30 s: se esperaba la lista de personajes"));
}

// La elección espera al jugador: no tiene tope.
TEST(ServerSessionTest, ChoosingCharacterHasNoTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.PollEachFrameUntil(ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
}

// La respuesta del F3 03 es de la CL7 (0x4c3940): hasta entonces, la entrada no tiene tope.
TEST(ServerSessionTest, EnteringWorldHasNoTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.EnterWorld();

    tested.PollEachFrameUntil(ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
}

TEST(ServerSessionTest, AStartForgetsTheList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{0});

    tested.session.Start();

    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
}

TEST(ServerSessionTest, LosingTheGameServerWhileChoosingACharacterFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());

    tested.socket.peerClosed = true;
    tested.session.Poll();

    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// Los nombres son del jugador: el log dice lugares (como el de 0x636290 sin el nombre).
TEST(ServerSessionTest, TheLogDoesNotHaveTheCharacterNames)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();

    EXPECT_FALSE(std::ranges::any_of(tested.sink.lines, [](const std::string& line)
                                     { return line.find("char000") != std::string::npos; }));
}

} // namespace
