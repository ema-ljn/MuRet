#include "App/ServerSession.h"

#include "App/CharacterEvent.h"
#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/GameLoop.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
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
#include "Protocol/Inventory.h"
#include "Protocol/Messages.h"
#include "Protocol/Movement.h"
#include "Protocol/OptionData.h"
#include "Protocol/Pet/PetOrders.h"
#include "Protocol/Skill.h"
#include "Protocol/World.h"
#include "Protocol/Character/CharacterCreate.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterSelect.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
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
#include "support/FakeSocket.h"
#include "support/IdleScene.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
using Client::App::WorldEvent;
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
using Client::Protocol::Character::CharacterSelectResult;
using Client::Protocol::Character::CreateSubcode;
using Client::Protocol::Character::DeleteCodeSize;
using Client::Protocol::Character::DeleteSubcode;
using Client::Protocol::Character::InventoryList;
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
constexpr std::string_view KeepaliveSent = "se encoló el keepalive";

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
          session(connection, clock, log, GameClientKeys(), std::move(settings), eventSink)
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
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
    }

    // Y la lista, de un servidor: espera la elección.
    void ReachChoosingServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachServerList());
        socket.reads = {ServerList({LabServerCode})};
        static_cast<void>(session.Poll());
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
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::ConnectingGameServer);
        gameServerStart = socket.sent.size();
    }

    // Y el Opened del GS: espera su saludo.
    void ReachGameHello()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameServer());
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::WaitingGameHello);
    }

    // Y el saludo, cifrado como lo manda el GS del laboratorio: espera la cuenta y la clave.
    void ReachWaitingCredentials()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameHello());
        socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), gameServerSends)};
        static_cast<void>(session.Poll());
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
        static_cast<void>(session.Poll());
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
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::ChoosingCharacter);
    }

    // Y la elección del lugar 0 y la entrada: el F3 03 salió (0x3c).
    void ReachEnteringWorld()
    {
        ASSERT_NO_FATAL_FAILURE(ReachChoosingCharacter());
        session.ChooseCharacter(std::uint8_t{0});
        session.EnterWorld();
        ASSERT_EQ(session.State(), SessionState::EnteringWorld);
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
        static_cast<void>(session.Poll());
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
            static_cast<void>(session.Poll());
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
    Client::Tests::RecordingSessionEventSink eventSink;
    ServerSession session;
    std::size_t gameServerStart = 0; // dónde empieza en `socket.sent` lo que va al GS
    PacketSerials gameServerSends;   // los seriales de lo que cifra el GS
};

// El esquema de la alternativa real debe poseer entradas y siete bytes opacos, no vistas al Packet.
// El runtime de abajo comprueba lifecycle y valores; por sí solo no demuestra ausencia de vistas.
static_assert(std::is_same_v<std::variant_alternative_t<1, WorldEvent>, InventoryList>);
static_assert(std::is_same_v<decltype(InventoryList::items), std::vector<Client::Protocol::Character::InventoryEntry>>);
static_assert(std::is_same_v<decltype(Client::Protocol::Character::InventoryEntry::item), std::array<std::byte, 7>>);

// Wire sintético independiente de lectores/writers: mínimo 0x34, padding distinto y 23 campos.
ByteVector WorldEntryReply()
{
    return Bytes({0xC1, 0x34, 0xF3, 0x03, 0x81, 0x2A, 0x20, 0x00, 0xEF, 0xCD, 0xAB, 0x89, 0x76,
                  0x54, 0x32, 0x10, 0x11, 0x91, 0x22, 0xA2, 0x33, 0xB3, 0x44, 0xC4, 0x55, 0xD5,
                  0x66, 0xE6, 0x77, 0xF7, 0x88, 0x18, 0x99, 0x29, 0xAA, 0x3A, 0xBB, 0x4B, 0xDE,
                  0xAD, 0x98, 0xBA, 0xDC, 0xFE, 0x80, 0xFE, 0xCC, 0x5C, 0xDD, 0x6D, 0xFF, 0xFF});
}

ByteVector OtherWorldEntryReply()
{
    ByteVector bytes = WorldEntryReply();
    bytes[4] = std::byte{0};
    bytes[5] = std::byte{0xFF};
    bytes[6] = std::byte{0xFF};
    bytes[7] = std::byte{0xFF};
    return bytes;
}

// C2 se cifra como C4 incluso siendo corto (support/TestGameServer.h); ningún dato es un ítem real.
ByteVector InventoryReply()
{
    return Bytes({0xC2, 0x00, 0x1E, 0xF3, 0x10, 0x03, 0xFF, 0x00, 0x80, 0xFF, 0x01, 0x7F, 0xAA, 0x55, 0x0C,
                  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0xFF, 0x81, 0x02, 0x40, 0xF0, 0x0F, 0xCC, 0x33});
}

ByteVector EmptyInventoryReply()
{
    return Bytes({0xC2, 0x00, 0x06, 0xF3, 0x10, 0x00});
}

void ExpectWorldEntry(const WorldEvent& event)
{
    const auto* entry = std::get_if<CharacterSelectResult>(&event);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->x, 0x81);
    EXPECT_EQ(entry->y, 0x2A);
    EXPECT_EQ(entry->map, 32); // La recepción no aplica el remapeo 32 -> 9.
    EXPECT_EQ(entry->direction, 0);
    EXPECT_EQ(entry->experience, 0x89ABCDEFu);
    EXPECT_EQ(entry->nextExperience, 0x10325476u);
    EXPECT_EQ(entry->levelUpPoints, 0x9111);
    EXPECT_EQ(entry->strength, 0xA222);
    EXPECT_EQ(entry->agility, 0xB333);
    EXPECT_EQ(entry->vitality, 0xC444);
    EXPECT_EQ(entry->energy, 0xD555);
    EXPECT_EQ(entry->life, 0xE666);
    EXPECT_EQ(entry->maxLife, 0xF777);
    EXPECT_EQ(entry->mana, 0x1888);
    EXPECT_EQ(entry->maxMana, 0x2999);
    EXPECT_EQ(entry->bp, 0x3AAA);
    EXPECT_EQ(entry->maxBp, 0x4BBB);
    EXPECT_EQ(entry->zen, 0xFEDCBA98u);
    EXPECT_EQ(entry->pk, 0x80);
    EXPECT_EQ(entry->controlCode, 0xFE);
    EXPECT_EQ(entry->addedPoints, 0x5CCC);
    EXPECT_EQ(entry->maxAddedPoints, 0x6DDD);
    EXPECT_EQ(entry->leadership, 0xFFFF);
}

void ExpectInventory(const WorldEvent& event)
{
    const auto* inventory = std::get_if<InventoryList>(&event);
    ASSERT_NE(inventory, nullptr);
    ASSERT_EQ(inventory->items.size(), 3u);
    EXPECT_EQ(inventory->items[0].slot, 0xFF);
    EXPECT_TRUE(SameBytes(inventory->items[0].item, Bytes({0x00, 0x80, 0xFF, 0x01, 0x7F, 0xAA, 0x55})));
    EXPECT_EQ(inventory->items[1].slot, 0x0C);
    EXPECT_TRUE(SameBytes(inventory->items[1].item, Bytes({0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32})));
    EXPECT_EQ(inventory->items[2].slot, 0xFF);
    EXPECT_TRUE(SameBytes(inventory->items[2].item, Bytes({0x81, 0x02, 0x40, 0xF0, 0x0F, 0xCC, 0x33})));
}

ByteVector EncryptedWorldSequence(Harness& tested)
{
    return Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                 FromTheGameServer(OtherWorldEntryReply(), tested.gameServerSends)});
}

void ExpectWorldSequence(std::span<const WorldEvent> events)
{
    ASSERT_EQ(events.size(), 3u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[1]));
    const auto* other = std::get_if<CharacterSelectResult>(&events[2]);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->x, 0);
    EXPECT_EQ(other->y, 0xFF);
    EXPECT_EQ(other->map, 0xFF);
    EXPECT_EQ(other->direction, 0xFF);
    EXPECT_EQ(other->experience, 0x89ABCDEFu);
    EXPECT_EQ(other->nextExperience, 0x10325476u);
    EXPECT_EQ(other->levelUpPoints, 0x9111);
    EXPECT_EQ(other->strength, 0xA222);
    EXPECT_EQ(other->agility, 0xB333);
    EXPECT_EQ(other->vitality, 0xC444);
    EXPECT_EQ(other->energy, 0xD555);
    EXPECT_EQ(other->life, 0xE666);
    EXPECT_EQ(other->maxLife, 0xF777);
    EXPECT_EQ(other->mana, 0x1888);
    EXPECT_EQ(other->maxMana, 0x2999);
    EXPECT_EQ(other->bp, 0x3AAA);
    EXPECT_EQ(other->maxBp, 0x4BBB);
    EXPECT_EQ(other->zen, 0xFEDCBA98u);
    EXPECT_EQ(other->pk, 0x80);
    EXPECT_EQ(other->controlCode, 0xFE);
    EXPECT_EQ(other->addedPoints, 0x5CCC);
    EXPECT_EQ(other->maxAddedPoints, 0x6DDD);
    EXPECT_EQ(other->leadership, 0xFFFF);
}

// El contexto se observa dentro del handler, antes de cualquier transición posterior.
TEST(ServerSessionTest, SynchronousDeliveryObservesContemporaryStatesAcrossPolls)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingGameHello, SessionState::WaitingCredentials}));
    tested.session.SubmitCredentials(Credentials{std::string(Account), std::string(Password)});
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(LoginResult(1), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingGameHello, SessionState::WaitingCredentials,
                                         SessionState::WaitingLoginResult, SessionState::LoggedIn}));
    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.eventSink.worldStates.size(), 5u);
    EXPECT_EQ(tested.eventSink.worldStates.back(), SessionState::LoggedIn);
}

TEST(ServerSessionTest, SynchronousDeliveryPreservesMixedOrderAndRawDuplicateSlots)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.eventSink.onCharacter = [](const CharacterEvent& event,
                                      const ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
        EXPECT_EQ(std::get<CharacterListReceived>(event).list.characters.size(), 2u);
        EXPECT_EQ(session.Characters()->characters.size(), 1u);
        EXPECT_EQ(session.Characters()->characters.back().name, NameBytes("char0002"));
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends),
                                 CharacterListBytes({{0, "char0001"}, {0, "char0002"}}),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"inventory", "list", "entry", "entry"}));
    ASSERT_EQ(tested.eventSink.world.size(), 3u);
    EXPECT_TRUE(std::get<InventoryList>(tested.eventSink.world[0]).items.empty());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(tested.eventSink.world[1]));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(tested.eventSink.world[2]));
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::WaitingCharacterList, SessionState::ChoosingCharacter,
                                         SessionState::ChoosingCharacter}));
    ASSERT_EQ(tested.session.Characters()->characters.size(), 1u);
    EXPECT_EQ(tested.session.Characters()->characters[0].name, Client::Tests::NameBytes("char0002"));
}

// La vista UI mantiene su contrato aunque falle el sink y el transporte ya haya cerrado.
TEST(ServerSessionTest, CharacterListApplicationFailureKeepsTheNormalizedViewAndPrecedesRemoteClose)
{
    for (const bool peerClosed : {false, true})
    {
        SCOPED_TRACE(peerClosed);
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
        tested.eventSink.onCharacter =
            [&tested, peerClosed](const CharacterEvent& event,
                                  const ServerSession& session) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
            EXPECT_EQ(tested.connection.State(), peerClosed ? LinkState::Closed : LinkState::Open);
            const auto& raw = std::get<CharacterListReceived>(event).list.characters;
            EXPECT_EQ(raw.size(), 2u);
            EXPECT_EQ(raw.front().name, NameBytes("char0001"));
            EXPECT_EQ(raw.back().name, NameBytes("char0002"));
            EXPECT_EQ(session.Characters()->characters.size(), 1u);
            EXPECT_EQ(session.Characters()->characters.back().name, NameBytes("char0002"));
            EXPECT_EQ(session.ChosenCharacter(), nullptr);
            return Client::Core::Unexpected(std::string("falló la lista"));
        };
        tested.socket.reads = {CharacterListBytes({{0, "char0001"}, {0, "char0002"}})};
        tested.socket.peerClosed = peerClosed;
        const auto polled = tested.session.Poll();
        ASSERT_FALSE(polled.HasValue());
        EXPECT_EQ(polled.Error(), "falló la lista");
        EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
        EXPECT_EQ(tested.session.State(), SessionState::Failed);
        EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list"}));
        ASSERT_EQ(tested.session.Characters()->characters.size(), 1u);
        EXPECT_EQ(tested.session.Characters()->characters.back().name, NameBytes("char0002"));
        EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    }
}

TEST(ServerSessionTest, SynchronousDeliveryFinishesBeforeRejectionAndRemoteClose)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.eventSink.onWorld = [&tested](const WorldEvent&,
                                         ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingGameHello);
        EXPECT_EQ(tested.connection.State(), LinkState::Closed);
        tested.eventSink.trace.push_back("applied");
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(0, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};
    tested.socket.peerClosed = true;
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"entry", "applied"}));
    EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected);
}

TEST(ServerSessionTest, SynchronousDeliveryRejectionBeforeWorldDropsTheSuffix)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(GameHello(0, ClientVersion), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.trace.empty());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::HelloRejected);
}

TEST(ServerSessionTest, SynchronousDeliverySecondErrorStopsBatchAndSuccessStateAndKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    tested.AdvanceTo(Client::App::KeepalivePeriod);
    const auto keepalives = tested.Keepalives().size();
    tested.eventSink.onCharacter = [](const CharacterEvent&,
                                      const ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("falla original")); };
    tested.socket.reads = {
        Join({FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends), CharacterListBytes({{0, "char0001"}}),
              FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    const auto polled = tested.session.Poll();
    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falla original");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"inventory", "list"}));
    EXPECT_EQ(tested.eventSink.characterStates, (std::vector<SessionState>{SessionState::WaitingCharacterList}));
    // Corrobora que no sale otro envío; el cierre por Fail también basta para impedirlo.
    EXPECT_EQ(tested.Keepalives().size(), keepalives);
    const auto again = tested.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), "falla original");
    EXPECT_EQ(tested.eventSink.trace.size(), 2u);
    tested.session.Start();
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
}

TEST(ServerSessionTest, SynchronousDeliveryWorldErrorStopsBeforeFollowingHello)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.eventSink.onWorld = [](const WorldEvent&, ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("falta aplicador World")); };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(GameHello(1, ClientVersion), tested.gameServerSends)})};
    const auto polled = tested.session.Poll();
    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falta aplicador World");
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"entry"}));
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
}

TEST(ServerSessionTest, SynchronousDeliveryQuitBeforeWorldDropsTheSuffixAndRemainsTerminal)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(LogoutResult(0), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends)})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_TRUE(tested.eventSink.trace.empty());
    const auto starts = tested.session.StartCount();
    tested.session.Start();
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_EQ(tested.session.StartCount(), starts);
}

TEST(ServerSessionTest, SynchronousDeliveryCompletesTheFakeLoadBeforeTheNextMixedEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    int loads = 0;
    tested.eventSink.onWorld = [&tested, &loads](const WorldEvent&,
                                                 ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::WaitingCharacterList);
        tested.eventSink.trace.push_back("load-begin");
        // Doble síncrono: prueba la frontera y el retorno, no equivale al loader nativo.
        ++loads;
        tested.eventSink.trace.push_back("load-end");
        return {};
    };
    tested.eventSink.onCharacter = [&loads](const CharacterEvent&,
                                            const ServerSession&) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(loads, 2);
        return {};
    };
    tested.socket.reads = {
        Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
              FromTheGameServer(WorldEntryReply(), tested.gameServerSends), CharacterListBytes({})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.trace,
              (std::vector<std::string>{"entry", "load-begin", "load-end", "entry", "load-begin", "load-end", "list"}));
}

// Con collector explícito, la recepción conserva el wire sin aplicar el mundo ni alterar la UI.
TEST(ServerSessionTest, WorldEntryPreservesAllWireFieldsWithoutApplyingSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);
    const CharacterEntry* chosen = tested.session.ChosenCharacter();
    const std::uint32_t starts = tested.session.StartCount();
    const ByteVector sent = tested.socket.sent;
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.eventSink.worldStates, (std::vector<SessionState>{SessionState::EnteringWorld}));
    EXPECT_EQ(tested.session.ChosenCharacter(), chosen);
    EXPECT_EQ(tested.session.StartCount(), starts);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
}

// Antes del saludo también conserva slots repetidos y cantidad cero sin filtrar entradas.
TEST(ServerSessionTest, WorldInventoryPreservesEveryEntryAndPublishesAnEmptyList)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameHello());
    tested.socket.reads = {Join({FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[0]));
    const auto* empty = std::get_if<InventoryList>(&events[1]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// Una cola por tipo perdería el orden intercalado aunque cada payload estuviera intacto.
TEST(ServerSessionTest, WorldEventsPreserveInterleavedOrderInOneBatch)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};

    static_cast<void>(tested.session.Poll());

    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Dos recepciones iguales siguen siendo dos eventos; no corresponde deduplicar aquí.
TEST(ServerSessionTest, WorldEventsKeepIdenticalRepeatedReceptions)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 4u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[1]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[2]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[3]));
}

// Cada corte TCP debe producir la misma secuencia, aun con un paquete partido entre Poll.
TEST(ServerSessionTest, WorldEventsPreserveInterleavedOrderAcrossEveryReadSplit)
{
    Harness baseline(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(baseline.ReachLoggedIn());
    const std::size_t wireSize = EncryptedWorldSequence(baseline).size();
    for (std::size_t split = 1; split < wireSize; ++split)
    {
        SCOPED_TRACE(split);
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        const ByteVector wire = EncryptedWorldSequence(tested);
        const auto middle = wire.begin() + static_cast<std::ptrdiff_t>(split);
        tested.socket.reads = {ByteVector(wire.begin(), middle)};
        static_cast<void>(tested.session.Poll());
        tested.socket.reads = {ByteVector(middle, wire.end())};
        static_cast<void>(tested.session.Poll());
        ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
        EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    }
}

// Dos Poll entregan en orden, antes de que el Closed final cambie el estado de la sesión.
TEST(ServerSessionTest, WorldReceptionsAreDeliveredAcrossPollsBeforeTheRemoteClose)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::LoggedIn);

    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(tested.eventSink.worldStates, std::vector<SessionState>(4, SessionState::LoggedIn));
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(std::span(events).first(3)));
    const auto* empty = std::get_if<InventoryList>(&events[3]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// Un cuerpo válido en claro no acredita la procedencia cifrada que exige esta frontera: del F3 03 no
// llega nada al sink, y de cada F3 10 solo la inicialización común de 0x4c55a0, sin el cuerpo. Cada uno le
// avisa al GS con el F1 03 00 F3, el F3 10 después de vaciar.
TEST(ServerSessionTest, WorldEventsRejectClearPacketsAfterTheEncryptedPositive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {Join({WorldEntryReply(), InventoryReply(), EmptyInventoryReply()})};
    static_cast<void>(tested.session.Poll());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Client::App::ClearInventoryReceived>(events[0]));
    EXPECT_TRUE(std::holds_alternative<Client::App::ClearInventoryReceived>(events[1]));
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 3);
    for (std::size_t report = sentBefore; report < received.packets.size(); ++report)
    {
        EXPECT_TRUE(
            SameBytes(Xor32Decode(received.packets[report]).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF3})));
    }
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// 0x4c55a0 sin cifrar: si el sink no puede vaciar, la sesión falla y no manda el aviso.
TEST(ServerSessionTest, AClearInventoryThatFailsToApplyIsNotReported)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.eventSink.onWorld = [](const WorldEvent&, ServerSession&) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("sin inventario")); };

    tested.socket.reads = {EmptyInventoryReply()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
}

// 0x4c396a: la respuesta del F3 03 sin cifrar no entra al mundo y le avisa al GS con el F1 03 00 F3.
TEST(ServerSessionTest, AClearWorldEntryReplyIsReportedWithF103)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {WorldEntryReply()};
    static_cast<void>(tested.session.Poll());

    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF3})));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// CL9 hito 4: las skills que llegan cifradas entran al mundo, cada una con su tipo.
TEST(ServerSessionTest, TheEncryptedSkillsReachTheWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({
        FromTheGameServer(Bytes({0xC1, 0x08, 0x19, 0x0D, 0x12, 0x34, 0x80, 0x56}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x09, 0x1E, 0x0E, 0x12, 0x34, 0x80, 0x81, 0x40}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x06, 0x1B, 0x1B, 0x12, 0x34}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x0A, 0x07, 0x01, 0x10, 0x00, 0x00, 0x00, 0x12, 0x34}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x06, 0x48, 0x12, 0x34, 0x10}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x08, 0x69, 0x03, 0x34, 0x12, 0x56, 0x00}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x00, 0x02, 0x11}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x05, 0xF3, 0x20, 0x42}), tested.gameServerSends),
        FromTheGameServer(Bytes({0xC1, 0x04, 0x73, 0x00}), tested.gameServerSends),
    })};
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    static_cast<void>(tested.session.Poll());
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 8u);
    const auto* attack = std::get_if<Client::Protocol::Skill::MagicAttack>(&events[0]);
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(attack->source, 0x1234);
    EXPECT_EQ(attack->target, 0x0056);
    EXPECT_TRUE(attack->succeeded);
    EXPECT_NE(std::get_if<Client::Protocol::Skill::DurationMagic>(&events[1]), nullptr);
    EXPECT_NE(std::get_if<Client::Protocol::Skill::MagicCancel>(&events[2]), nullptr);
    EXPECT_NE(std::get_if<Client::Protocol::Skill::ActorState>(&events[3]), nullptr);
    EXPECT_NE(std::get_if<Client::Protocol::Skill::ActorEffect>(&events[4]), nullptr);
    EXPECT_NE(std::get_if<Client::Protocol::Skill::SkillTarget>(&events[5]), nullptr);
    const auto* list = std::get_if<Client::Protocol::Skill::SkillList>(&events[6]);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->slot, 2);
    EXPECT_EQ(list->skill, 0x11);
    const auto* summon = std::get_if<Client::Protocol::Skill::SummonLife>(&events[7]);
    ASSERT_NE(summon, nullptr);
    EXPECT_EQ(summon->life, 0x42);
    // El 73 cifrado no hace nada (0x4d5b10).
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// 0x4cc562, 0x4ce032 y 0x4d5d8c: el 19, el 1E y el 73 sin cifrar no entran y avisan con el F1 03 00 [cabeza].
TEST(ServerSessionTest, TheClearSkillsAreReportedWithF103)
{
    for (const auto& [packet, head] : {std::pair{Bytes({0xC1, 0x08, 0x19, 0x0D, 0x12, 0x34, 0x80, 0x56}), 0x19},
                                       std::pair{Bytes({0xC1, 0x09, 0x1E, 0x0E, 0x12, 0x34, 0x80, 0x81, 0x40}), 0x1E},
                                       std::pair{Bytes({0xC1, 0x04, 0x73, 0x00}), 0xF1}})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        const std::size_t sentBefore = tested.GameServerReads().packets.size();
        tested.socket.reads = {packet};
        static_cast<void>(tested.session.Poll());
        const GameServerReceived received = tested.GameServerReads();
        ASSERT_EQ(received.packets.size(), sentBefore + 1) << head;
        EXPECT_TRUE(
            SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, head})));
        EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
        EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    }
}

// Los demás no miran el cifrado (0x4cbe90, 0x4cbe00, 0x4dbf50, 0x4cc290 y 0x4c5070).
TEST(ServerSessionTest, TheOtherClearSkillsReachTheWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {
        Join({Bytes({0xC1, 0x06, 0x1B, 0x1B, 0x12, 0x34}), Bytes({0xC1, 0x08, 0x69, 0x03, 0x34, 0x12, 0x56, 0x00}),
              Bytes({0xC1, 0x07, 0xF3, 0x11, 0xFF, 0x00, 0x02})})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.eventSink.TakeWorldEvents().size(), 3u);
}

// 0x4e4c0a y 0x4e4c42: las órdenes del cuervo entran al mundo sin mirar el cifrado.
TEST(ServerSessionTest, TheRavenOrdersReachTheWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({Bytes({0xC1, 0x07, 0xA7, 0x00, 0x03, 0x12, 0x34}),
                                 Bytes({0xC1, 0x09, 0xA8, 0x00, 0x01, 0x12, 0x34, 0x05, 0x67})})};
    static_cast<void>(tested.session.Poll());
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    const auto* order = std::get_if<Client::Protocol::Pet::PetOrder>(&events[0]);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->order, 3);
    EXPECT_EQ(order->key, 0x1234);
    const auto* attack = std::get_if<Client::Protocol::Pet::PetAttack>(&events[1]);
    ASSERT_NE(attack, nullptr);
    EXPECT_EQ(attack->kind, 1);
    EXPECT_EQ(attack->owner, 0x1234);
    EXPECT_EQ(attack->target, 0x0567);
}

// 0x4c1ec0: el F1 00 aceptado deja la clave del jugador (pkt[5..6], big endian) para el F3 03.
TEST(ServerSessionTest, TheAcceptedGameHelloKeepsThePlayerIndex)
{
    Harness tested(IdentityObfuscation::Shared());
    EXPECT_EQ(tested.session.PlayerIndex(), 0);

    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    EXPECT_EQ(tested.session.PlayerIndex(), 5);
}

// Un checksum válido no vuelve legible un cuerpo corto o una cantidad imposible.
TEST(ServerSessionTest, WorldEventsRejectEncryptedTruncationsAfterThePositive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));

    const std::size_t packetsSentBeforeCuts = tested.GameServerReads().packets.size();
    // Cada corte conserva hasta el subcódigo: llega al handler, falla el cuerpo, no el checksum.
    std::size_t rejectedEntries = 0;
    std::size_t rejectedInventories = 0;
    for (const ByteVector& valid : {WorldEntryReply(), InventoryReply()})
    {
        const bool wide = valid[0] == std::byte{0xC2};
        const std::size_t minimum = wide ? 5u : 4u;
        for (std::size_t size = minimum; size < valid.size(); ++size)
        {
            SCOPED_TRACE(size);
            ByteVector shortReply(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(size));
            shortReply[wide ? 2u : 1u] = static_cast<std::byte>(size);
            tested.socket.reads = {FromTheGameServer(shortReply, tested.gameServerSends)};
            static_cast<void>(tested.session.Poll());
            EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
            ++(wide ? rejectedInventories : rejectedEntries);
        }
    }
    ByteVector badCount = InventoryReply();
    badCount[5] = std::byte{0xFF};
    tested.socket.reads = {FromTheGameServer(badCount, tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    ++rejectedInventories;
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "(F3 03): no se puede leer"), rejectedEntries);
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "(F3 10): no se puede leer"), rejectedInventories);
    // Con checksum correcto no corresponde enviar nada, incluido el F1/03 por descifrado fallido.
    EXPECT_EQ(tested.GameServerReads().packets.size(), packetsSentBeforeCuts);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Cifrar un tipo u opcode incorrecto no debe convertirlo en una respuesta de mundo.
TEST(ServerSessionTest, WorldEventsDistinguishPacketKindsAndSubcodes)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));

    ByteVector wideEntry = WorldEntryReply();
    wideEntry.insert(wideEntry.begin() + 1, std::byte{0});
    wideEntry[0] = std::byte{0xC2};
    wideEntry[2] = static_cast<std::byte>(wideEntry.size());
    ByteVector narrowInventory = InventoryReply();
    narrowInventory.erase(narrowInventory.begin() + 1);
    narrowInventory[0] = std::byte{0xC1};
    narrowInventory[1] = static_cast<std::byte>(narrowInventory.size());
    std::vector<ByteVector> wrongKinds{wideEntry, narrowInventory};
    for (ByteVector packet : {WorldEntryReply(), InventoryReply()})
    {
        const std::size_t opcode = packet[0] == std::byte{0xC1} ? 2u : 3u;
        ByteVector otherOpcode = packet;
        otherOpcode[opcode] = std::byte{0xF2};
        wrongKinds.push_back(std::move(otherOpcode));
        packet[opcode + 1] = std::byte{0x7F};
        wrongKinds.push_back(std::move(packet));
    }
    for (const ByteVector& packet : wrongKinds)
    {
        tested.socket.reads = {FromTheGameServer(packet, tested.gameServerSends)};
        static_cast<void>(tested.session.Poll());
        EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    }
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// El cierre posterior del peer no debe borrar lo recibido antes en el mismo lote.
TEST(ServerSessionTest, WorldEventsSurviveFailureInTheSamePoll)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.eventSink.onWorld = [&tested](const WorldEvent&,
                                         ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::LoggedIn);
        EXPECT_EQ(tested.connection.State(), LinkState::Closed);
        return {};
    };
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    tested.socket.peerClosed = true;

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// Start descarta el paquete incompleto, pero no las copias ya entregadas al collector.
TEST(ServerSessionTest, WorldEventsStartPreservesDeliveredCopiesAndDropsPartiallyReceivedPackets)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    const ByteVector entry = FromTheGameServer(WorldEntryReply(), tested.gameServerSends);
    ByteVector partial = FromTheGameServer(InventoryReply(), tested.gameServerSends);
    partial.resize(partial.size() / 2);
    tested.socket.reads = {Join({entry, partial})};
    static_cast<void>(tested.session.Poll());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));

    tested.session.Start();

    const auto deliveredBeforeStart = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(deliveredBeforeStart.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(deliveredBeforeStart[0]));
    tested.gameServerSends = PacketSerials{};
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    tested.socket.reads = {FromTheGameServer(EmptyInventoryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* empty = std::get_if<InventoryList>(&events[0]);
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->items.empty());
}

// QuitOrdered conserva lo previo y corta el resto del lote de esa conexión.
TEST(ServerSessionTest, WorldEventsStopAtTheExistingConnectionBatchBoundary)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(LogoutResult(0), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                                 FromTheGameServer(OtherWorldEntryReply(), tested.gameServerSends)})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// Los avisos del handler detectarían un registro accidental en CS, aun rechazando los claros.
TEST(ServerSessionTest, WorldEventsDoNotDispatchConnectServerPacketsAsGameServerPackets)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());
    tested.socket.reads = {Join(
        {WorldEntryReply(), InventoryReply(), AddressReply(GameHost, GamePort), WorldEntryReply(), InventoryReply()})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 10)"));
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 03)"));
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "(F3 10)"));

    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
}

// SetState(m_state) en el handler F3/03 reiniciaría la espera: debe vencer en el tope original.
TEST(ServerSessionTest, WorldEntryDoesNotExtendTheOriginalReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));

    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
}

// El F3/10 se ejercita solo: extender el deadline en cualquiera de los dos handlers debe fallar.
TEST(ServerSessionTest, WorldInventoryDoesNotExtendTheOriginalReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.AdvanceTo(seconds{5});
    tested.session.RequestCharacterList();
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    tested.socket.reads = {FromTheGameServer(InventoryReply(), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[0]));

    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
}

// Recibir o drenar no debe reiniciar la fase global ni cambiar velocidades/bytes del keepalive.
TEST(ServerSessionTest, WorldEventsPreserveTheNextKeepaliveBytesAndTimerPhase)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.SetKeepaliveSpeeds(
        KeepaliveSpeeds{.attackSpeed = 0x033C, .magicSpeed = 0x027C, .characterFlags = KeepaliveAdjust20});
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(milliseconds{11360});
    tested.socket.reads = {EncryptedWorldSequence(tested)};
    static_cast<void>(tested.session.Poll());
    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    ASSERT_EQ(tested.Keepalives().size(), 1u);

    tested.AdvanceTo(milliseconds{19999});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());

    const std::vector<Packet> sent = tested.Keepalives();
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_TRUE(
        SameBytes(sent[1].Bytes(), Bytes({0xC1, 0x0C, 0x0E, 0x00, 0x20, 0x4E, 0x00, 0x00, 0x28, 0x03, 0x68, 0x02})));
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// La procedencia cifrada debe sobrevivir a la máscara entrante antes de framing y descifrado.
TEST(ServerSessionTest, WorldEventsSurviveTheRealFoxAntiTransport)
{
    const FoxAntiObfuscation obfuscation;
    Harness tested(obfuscation);
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    ByteVector wire = EncryptedWorldSequence(tested);
    for (std::byte& value : wire)
    {
        value ^= std::byte{0xAA}; // Máscara entrante del laboratorio antes del framing/descifrado.
    }
    tested.socket.reads = {wire};
    static_cast<void>(tested.session.Poll());

    ASSERT_NO_FATAL_FAILURE(ExpectWorldSequence(tested.eventSink.TakeWorldEvents()));
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, StartsIdle)
{
    Harness tested(IdentityObfuscation::Shared());

    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_TRUE(tested.socket.sent.empty());

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, PetInfoRequest()));
}

// El test del 04: después de C1 04 00 01 sale C1 04 F4 02, exacto.
TEST(ServerSessionTest, AsksForTheListAfterTheHello)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());

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

    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    tested.socket.reads = {LabServerList()};
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseServer(LabServerCode);
    const ByteVector sent = tested.socket.sent;
    tested.session.ChooseServer(401);

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    EXPECT_EQ(tested.session.ChosenServer(), std::optional<std::uint16_t>{LabServerCode});
    EXPECT_TRUE(SameBytes(tested.socket.sent, sent));
    EXPECT_EQ(tested.Warnings(), (Lines{"[warning] se ignora la elección de un servidor: la sesión no la espera",
                                        "[warning] se ignora la elección de un servidor: la sesión no la espera"}));
}

// Los créditos (0x641203 y 0x63f9a0): C1 04 F4 06 en claro por el socket abierto, sin cambiar de estado; cada
// cierre manda otro.
TEST(ServerSessionTest, TheCreditsSendF406)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({401, 400})};
    static_cast<void>(tested.session.Poll());

    tested.session.SendCreditsNotice();
    tested.session.SendCreditsNotice();

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({PetInfoRequest(), ListRequest(), Bytes({0xC1, 0x04, 0xF4, 0x06}),
                                                    Bytes({0xC1, 0x04, 0xF4, 0x06})})));
}

// Sin un socket abierto, nada: el original no manda con el socket en −1.
TEST(ServerSessionTest, TheCreditsNeedAnOpenSocket)
{
    Harness tested(IdentityObfuscation::Shared());

    tested.session.SendCreditsNotice();

    EXPECT_TRUE(tested.socket.sent.empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "no se manda el F4 06 de los créditos"));
}

// La dirección del F4 03: la sesión la guarda, cierra el CS y conecta al GS, sin avisos.
TEST(ServerSessionTest, ReadsTheGameServerAddress)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerAddress());

    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingGameServer);

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
}

// Con el GameServer, el 00 va a su dispatcher, que lo tiene como el chat (0x4e2b10, CL8 hito 3): no es el saludo del
// CS y la sesión no pide otra lista. Lo que aplica decide con la escena: en la 2, 0x4c6070 pide la lista por el GS
// sin leer el paquete (0x4c6098).
TEST(ServerSessionTest, WithTheGameServerThe00IsTheChat)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachGameServer());
    const ByteVector sentToTheConnectServer = tested.socket.sent;

    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_TRUE(SameBytes(tested.socket.sent, sentToTheConnectServer));
    EXPECT_EQ(tested.Warnings(), Lines{});
    const auto events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NE(std::get_if<Client::Protocol::Messages::Chat>(&events[0]), nullptr);

    tested.session.ResendServerListRequest();
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({sentToTheConnectServer, ListRequest()})));
}

// Pregunta 9: una lista vacía es un error que lo dice, y la sesión cierra el CS.
TEST(ServerSessionTest, AnEmptyListFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {Bytes({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x00})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.socket.closes, 1);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la lista de servidores está vacía"));

    // El cierre lo pidió la sesión: no vuelve como un ConnectServerLost.
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    EXPECT_EQ(tested.session.GameServer(), std::nullopt);
    EXPECT_EQ(tested.session.ChosenServer(), std::nullopt);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.socket.connects.size(), 1u);
    EXPECT_TRUE(SameBytes(tested.socket.sent, Join({sent, ListRequest()})));
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el servidor elegido está ocupado: se pide la lista otra vez"));

    tested.socket.reads = {ServerList({401})};
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_EQ(tested.Warnings(), Lines{"[warning] se descarta un paquete mal formado del ConnectServer"});
}

TEST(ServerSessionTest, LosingTheConnectServerFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Y la de la cuenta: un GS que cierra con la cuenta en pantalla es un GameServerLost.
TEST(ServerSessionTest, LosingTheGameServerWhileWaitingForTheCredentialsFails)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCredentials());

    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// También mientras conecta: un CS que rechaza la conexión.
TEST(ServerSessionTest, AConnectServerThatRefusesFails)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.socket.connectResult = SocketError::Refused;

    tested.session.Start();
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ConnectingGameServer);
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.sendError = SocketError::Closed;
    tested.socket.reads = {Join({Hello(), ServerList({LabServerCode})})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "se descarta un C1 F4 que iba a salir"));

    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Si el CS cierra en el mismo Poll en que llega un paquete, el paquete se despacha y después el
// cierre: la sesión no se queda esperando una lista que no va a llegar.
TEST(ServerSessionTest, WhatArrivesBeforeTheServerClosesIsHandled)
{
    Harness tested(IdentityObfuscation::Shared());
    tested.session.Start();
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.socket.reads = {Hello()};
    tested.socket.peerClosed = true;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ConnectServerLost);
}

// Start vuelve a empezar desde cualquier estado, y olvida la falla y el GameServer de antes.
TEST(ServerSessionTest, StartingAgainStartsClean)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());
    tested.socket.reads = {ServerList({})};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Failure(), SessionFailure::EmptyServerList);

    tested.session.Start();
    EXPECT_EQ(tested.session.State(), SessionState::ConnectingConnectServer);
    EXPECT_EQ(tested.session.Failure(), std::nullopt);
    tested.socket.reads = {Hello(), ServerList({LabServerCode})};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingServer);
    tested.session.ChooseServer(LabServerCode);
    tested.socket.reads = {AddressReply(GameHost, GamePort)};
    // El primer Poll lee la dirección y abre el GS; el segundo da su Opened.
    static_cast<void>(tested.session.Poll());
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
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

    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
        static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::VersionMismatch);
    EXPECT_TRUE(tested.Logged(LogLevel::Error, "la versión 1???9 y este cliente la 10209"));
}

TEST(ServerSessionTest, AnAcceptedLoginIsLoggedIn)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    EXPECT_EQ(tested.session.LoginResult(), std::nullopt);

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    const auto expected = WriteLoginRequest(LoginRequest{Account, Password, TickCountMs(tested.clock.Now())});
    ASSERT_TRUE(expected.HasValue());
    EXPECT_TRUE(SameBytesWithoutValues(received.packets.back().Bytes(), expected.Value().packet.Bytes()));
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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingLoginResult);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), 2u);
    EXPECT_EQ(received.serials, (std::vector<SerialCheck>{SerialCheck::InOrder, SerialCheck::InOrder}));
    EXPECT_TRUE(SameBytesWithoutValues(received.packets[1].Bytes(), NoticeBytes()));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se le avisa al GameServer (F1 03)"));

    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

// Lo que no mide bloques enteros también lleva el aviso, como dice el contrato.
TEST(ServerSessionTest, APacketThatIsNotWholeBlocksSendsTheReport)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());

    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    EXPECT_EQ(tested.socket.sent.size(), tested.gameServerStart);

    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    tested.session.SubmitCredentials(TheCredentials());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF1, 0x01})};
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
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

// Con 2 (la lista), el original cambia de escena (Q-C79): hasta la CL9, la sesión lo ignora con un aviso
// y sigue. Con otro que 0, 1 o 2, el original no hace nada, y acá, lo mismo con su aviso. Sin el
// resultado, no se puede leer.
TEST(ServerSessionTest, TheOtherLogoutResultsAreIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());

    tested.socket.reads = {FromTheGameServer(LogoutResult(2), tested.gameServerSends),
                           FromTheGameServer(LogoutResult(3), tested.gameServerSends), Bytes({0xC1, 0x04, 0xF1, 0x02})};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    const auto ignored = [](int result)
    {
        return std::format("[warning] se ignora la orden de salida del GameServer (F1 02 con {}): MuRet sigue la de "
                           "cerrar el juego, con 0, y la de volver a la selección, con 1",
                           result);
    };
    EXPECT_EQ(tested.Warnings(),
              (Lines{ignored(2), ignored(3),
                     "[warning] se ignora la orden de salida del GameServer (F1 02): no se puede leer"}));
    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
}

// Lo que le llegó al GS desde `from`, sin el XOR32.
std::vector<Packet> GameServerPacketsFrom(const Harness& tested, std::size_t from)
{
    const GameServerReceived received = tested.GameServerReads();
    std::vector<Packet> packets;
    for (std::size_t index = from; index < received.packets.size(); ++index)
    {
        packets.push_back(Xor32Decode(received.packets[index]));
    }
    return packets;
}

// En el mundo, con el F3 03 publicado por el sink.
void ReachInWorld(Harness& tested)
{
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent& event,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<CharacterSelectResult>(event))
        {
            session.PublishWorldEntry();
        }
        return {};
    };
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.session.State(), SessionState::InWorld);
    static_cast<void>(tested.eventSink.TakeWorldEvents());
}

// 0x4c3626..0x4c390f: el F1 02 con 1 cifrado le da al sink la vuelta a la selección, con la sesión todavía
// en el mundo, y después salen el F3 00 (0x4c3653) y los dos C1 03 31 (0x419900 y 0x4499b0), en ese orden;
// la lista vieja y la elección se olvidan y la sesión espera la nueva. Con ella, se elige y se entra otra
// vez como la primera.
TEST(ServerSessionTest, AnEncryptedLogoutWithOneGoesToTheSelection)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {FromTheGameServer(LogoutResult(1), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    const std::vector<WorldEvent> events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<Client::App::ReturnToSelectionReceived>(events[0]));
    EXPECT_EQ(tested.eventSink.worldStates.back(), SessionState::InWorld);
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_FALSE(tested.session.JoinedWorld());
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    const std::vector<Packet> sent = GameServerPacketsFrom(tested, sentBefore);
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_TRUE(SameBytes(sent[0].Bytes(), Bytes({0xC1, 0x04, 0xF3, 0x00})));
    EXPECT_TRUE(SameBytes(sent[1].Bytes(), Bytes({0xC1, 0x03, 0x31})));
    EXPECT_TRUE(SameBytes(sent[2].Bytes(), Bytes({0xC1, 0x03, 0x31})));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "el GameServer ordenó volver a la selección (F1 02 con 1)"));

    // La selección nueva no vuelve a pedir la lista: la sesión no lo espera.
    tested.session.RequestCharacterList();
    EXPECT_EQ(GameServerPacketsFrom(tested, sentBefore).size(), 3u);
    tested.socket.reads = {CharacterListBytes(TwoCharacters())};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.EnterWorld();

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
    EXPECT_EQ(tested.CharacterPacketsSent(SelectSubcode).size(), 2u);
}

// 0x4c2f5b: sin cifrar, el 1 no limpia ni cambia de escena: solo le avisa al GS con el F1 03 00 F1, para
// cifrar (Q-C211). La sesión sigue en el mundo y el sink no recibe nada.
TEST(ServerSessionTest, AClearOneChangesNothing)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    const std::size_t encryptedBefore = tested.GameServerReads().serials.size();

    tested.socket.reads = {LogoutResult(1)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_TRUE(tested.eventSink.TakeWorldEvents().empty());
    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_TRUE(tested.session.Characters().has_value());
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xC1, 0x06, 0xF1, 0x03, 0x00, 0xF1})));
    EXPECT_EQ(received.serials.size(), encryptedBefore + 1);
    EXPECT_EQ(tested.Warnings(),
              (Lines{"[warning] la orden de volver a la selección (F1 02 con 1) no llegó cifrada: se le avisa al "
                     "GameServer (F1 03) y se sigue igual"}));
}

// La clave del 03, como la manda el GS (GCCheckMainExeKeySend): C1 06 03, un relleno y la clave.
std::vector<std::byte> MainCheckKey(std::uint16_t key)
{
    return Bytes({0xC1, 0x06, 0x03, 0x00, static_cast<int>(key & 0xFFU), static_cast<int>(key >> 8U)});
}

// La clave de la tabla 0 con el rand() en 0 (EncryptCheckSumKey del GS).
constexpr std::uint16_t TableZeroKey = 0xB479;

// 0x4e16c0: al 03 del GS contesta en el mismo Poll con la suma del archivo que nombra la línea de
// comandos, como C3 (si no, el GS cierra a los 3 s). Con 23 bytes de 0x10 en adelante y la tabla 0, la
// suma de MainCheckTest.
TEST(ServerSessionTest, TheMainCheckIsAnsweredWithTheSumOfTheExecutable)
{
    Client::Tests::TemporaryFolder folder("main-check");
    const std::filesystem::path executable = folder.Path() / "Main.exe";
    {
        std::ofstream file(executable, std::ios::binary);
        for (int value = 0x10; value < 0x10 + 23; ++value)
            file.put(static_cast<char>(value));
    }
    SessionSettings settings = Settings();
    settings.mainCheckFile = executable;
    Harness tested(IdentityObfuscation::Shared(), settings);
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    const std::size_t encryptedBefore = tested.GameServerReads().serials.size();

    tested.socket.reads = {FromTheGameServer(MainCheckKey(TableZeroKey), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    const GameServerReceived received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), sentBefore + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(),
                          Bytes({0xC1, 0x08, 0x03, 0x00, 0x65, 0x74, 0x38, 0xA5})));
    EXPECT_EQ(received.serials.size(), encryptedBefore + 1);
    EXPECT_TRUE(tested.Warnings().empty());
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se contestó la suma del ejecutable (03)"));
}

// Sin el archivo (0x4aec28, el CreateFileA que falla) la suma va en 0, y también en claro desde el GS:
// el dispatcher no mira el cifrado. Una carpeta tampoco se abre.
TEST(ServerSessionTest, WithoutTheExecutableTheMainCheckSumIsZero)
{
    Client::Tests::TemporaryFolder folder("main-check-missing");
    for (const std::filesystem::path& missing : {folder.Path() / "Main.exe", folder.Path()})
    {
        SCOPED_TRACE(missing.string());
        SessionSettings settings = Settings();
        settings.mainCheckFile = missing;
        Harness tested(IdentityObfuscation::Shared(), settings);
        ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
        const std::size_t sentBefore = tested.GameServerReads().packets.size();

        tested.socket.reads = {MainCheckKey(TableZeroKey)};
        ASSERT_TRUE(tested.session.Poll().HasValue());

        const GameServerReceived received = tested.GameServerReads();
        ASSERT_EQ(received.packets.size(), sentBefore + 1);
        EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(),
                              Bytes({0xC1, 0x08, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00})));
        ASSERT_EQ(tested.Warnings().size(), 1U);
        EXPECT_NE(tested.Warnings().front().find("la suma del ejecutable (03) va en 0"), std::string::npos);
    }
}

// Un 03 sin la clave se ignora con un aviso y no contesta.
TEST(ServerSessionTest, AMainCheckWithoutItsKeyIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(ReachInWorld(tested));
    const std::size_t sentBefore = tested.GameServerReads().packets.size();

    tested.socket.reads = {Bytes({0xC1, 0x05, 0x03, 0x00, 0x79})};
    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
    EXPECT_EQ(tested.Warnings(),
              (Lines{"[warning] se ignora la clave de la suma del ejecutable (03): no se puede leer"}));
}

// El 1 no mira el estado: también con el login recién aceptado, como el original, que no mira la escena.
// Si el sink no puede limpiar, la sesión falla y no manda nada.
TEST(ServerSessionTest, ALogoutWithOneThatFailsToApplyIsNotFollowed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const std::size_t sentBefore = tested.GameServerReads().packets.size();
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.State(), SessionState::LoggedIn);
        return Client::Core::Unexpected(std::string("sin el mundo"));
    };

    tested.socket.reads = {FromTheGameServer(LogoutResult(1), tested.gameServerSends)};
    const auto polled = tested.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "sin el mundo");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.GameServerReads().packets.size(), sentBefore);
}

// El ConnectServer no tiene F1: su dispatcher no tiene la orden (CL4.3), y un F1 02 con 0 de él no
// cierra nada. El original tiene un dispatcher solo y cerraría; un CS no lo manda.
TEST(ServerSessionTest, TheConnectServerCannotOrderTheQuit)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachServerList());

    tested.socket.reads = {LogoutResult(0)};
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    const std::size_t sentBefore = tested.socket.sent.size();
    tested.socket.reads = {Bytes({0xC3, 0x05, 0x11, 0x22, 0x33})};
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(milliseconds{39999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    tested.gameServerStart = tested.socket.sent.size();
    static_cast<void>(tested.session.Poll());
    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(milliseconds{79999});
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{80});
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 1u);
    EXPECT_EQ(tested.GameServerReads().serials, (std::vector<SerialCheck>{SerialCheck::InOrder}));
}

// El envío inmediato de F1 01 aceptado y el timer vencido son productores independientes.
TEST(ServerSessionTest, TheLoginAndTheDueTimerCanEachSendAKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.AdvanceTo(seconds{20});
    ASSERT_TRUE(tested.Keepalives().empty());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    EXPECT_EQ(tested.GameServerReads().packets.size(), 3u);
    tested.AdvanceTo(milliseconds{39999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    tested.gameServerStart = tested.socket.sent.size();
    tested.AdvanceTo(seconds{20});
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.connection.State(), LinkState::Connecting);
    tested.AdvanceTo(seconds{21});
    static_cast<void>(tested.session.Poll());
    tested.AdvanceTo(seconds{22});
    tested.socket.reads = {GameHello(1, ClientVersion)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_TRUE(tested.Keepalives().empty());
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());
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
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts + 1);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_FALSE(tested.Logged(LogLevel::Warning, "que iba a salir: la conexión está cerrada"));
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// El peer puede cerrar después de entregar F1 01: no se registra como enviado lo descartado.
TEST(ServerSessionTest, AClosingLoginBatchDoesNotLogASentKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.socket.reads = {FromTheGameServer(LoginResult(1), tested.gameServerSends)};
    tested.socket.peerClosed = true;
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "que iba a salir: la conexión está cerrada"));
    EXPECT_EQ(tested.GameServerReads().packets.size(), 1u);
}

// Entre el 0E que cierra el GS y su Closed diferido, la UI no debe borrar el rechazo ni fingir login.
TEST(ServerSessionTest, AFailedPreloginKeepalivePreservesTheCredentialsStateUntilClosed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoginResult());
    tested.socket.reads = {FromTheGameServer(LoginResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    tested.AdvanceTo(seconds{20});
    tested.socket.sendError = SocketError::Closed;
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.connection.State(), LinkState::Closed);
    const std::size_t loginMessages = tested.CountLogged(LogLevel::Info, "se manda el login");
    const std::size_t sendAttempts = tested.socket.sendAttempts;
    tested.session.SubmitCredentials(TheCredentials());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCredentials);
    EXPECT_EQ(tested.session.LoginResult(), std::optional<std::uint8_t>{std::uint8_t{0}});
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "se manda el login"), loginMessages);
    EXPECT_EQ(tested.socket.sendAttempts, sendAttempts);
    EXPECT_EQ(tested.CountLogged(LogLevel::Info, "keepalive"), 0u);
    EXPECT_FALSE(tested.LoggedTheCredentials());
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
}

// Un frame de 60 s (un breakpoint) con el login en t = 3 s: en t = 63 s sale uno solo, con el
// serial 2, y el siguiente a los 83 s, sin recuperar los vencimientos de 20, 40 y 60 s.
TEST(ServerSessionTest, ASixtySecondFrameSendsOneKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.LogInAt(seconds{3}));

    tested.AdvanceTo(seconds{63});
    static_cast<void>(tested.session.Poll());
    static_cast<void>(tested.session.Poll());
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
                        tested.clock, [&tested] { return tested.session.Poll(); }, tested.log);
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingConnectHello);

    tested.AdvanceTo(ReplyTimeout - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingConnectHello);
    tested.AdvanceTo(ReplyTimeout);
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    tested.AdvanceTo(seconds{20});
    tested.socket.reads = {Hello()};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingServerList);

    tested.AdvanceTo(milliseconds{49999});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerList);
    tested.AdvanceTo(seconds{50});
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingServerAddress);
    tested.AdvanceTo(seconds{40});
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::WaitingGameHello);

    tested.AdvanceTo(seconds{35} - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingGameHello);
    tested.AdvanceTo(seconds{35});
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());

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
    EXPECT_EQ(infos,
              (Lines{"[info] se conecta al ConnectServer", "[info] conectó con el ConnectServer",
                     "[info] el ConnectServer saludó: se pide la lista de servidores",
                     "[info] la lista trae 1 servidores: se espera la elección",
                     "[info] se elige un servidor: se pide su dirección",
                     "[info] el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese",
                     "[info] la conexión se cerró: la cerró el cliente",
                     "[info] conectó con el GameServer: se espera su saludo",
                     "[info] el GameServer saludó: se espera la cuenta y la clave", "[info] se manda el login",
                     "[info] el GameServer aceptó el login", "[info] se encoló el keepalive para el GameServer"}));
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
    const auto events = tested.eventSink.TakeCharacterEvents();
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

    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// El lote posee cada campo, incluido el orden y los slots repetidos que admite el lector.
TEST(ServerSessionTest, TakingTheListEventPreservesItsPayloadAndConsumesItOnce)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());
    const std::vector<CharacterEntryFields> entries{
        {3, "char0002", 513, 0x81, 0x21, {0x12, 0x34, 0x56}, 0x72}, {0, "char0001"}, {3, "char0003"}};
    tested.socket.reads = {CharacterListBytes(entries, 3, 5, 7)};

    static_cast<void>(tested.session.Poll());
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();

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
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
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
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): llegó fuera de turno"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ACharacterListThatDoesNotReadIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachWaitingCharacterList());

    tested.socket.reads = {CharacterListBytes({{}}, 2)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    EXPECT_FALSE(tested.session.Characters().has_value());
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "se ignora la lista de personajes (F3 00): no se puede leer"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
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

TEST(ServerSessionTest, SynchronousDeliveryCreateAndDeleteFailuresKeepTheirPreviousEffectsWithoutSuccessTransition)
{
    for (const bool deleting : {false, true})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        tested.eventSink.characters.clear();
        tested.eventSink.trace.clear();
        tested.eventSink.onCharacter =
            [deleting](const CharacterEvent&, const ServerSession& session) -> Client::Core::Expected<void, std::string>
        {
            EXPECT_EQ(session.State(),
                      deleting ? SessionState::WaitingDeleteResult : SessionState::WaitingCreateResult);
            EXPECT_EQ(session.Characters()->characters.size(), deleting ? 1u : 3u);
            return Client::Core::Unexpected(std::string("falló el personaje"));
        };
        if (deleting)
        {
            tested.session.ChooseCharacter(std::uint8_t{0});
            tested.session.DeleteCharacter(SyntheticDeleteCode());
            tested.socket.reads = {Bytes({0xc1, 0x05, 0xf3, 0x02, 0x01})};
        }
        else
        {
            tested.session.ChooseCharacter(std::uint8_t{0});
            ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
            EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0001"));
            tested.session.CreateCharacter("char0003", 0);
            tested.socket.reads = {CreatedReply(1)};
        }
        const auto polled = tested.session.Poll();
        ASSERT_FALSE(polled.HasValue());
        EXPECT_EQ(polled.Error(), "falló el personaje");
        EXPECT_EQ(tested.session.State(), SessionState::Failed);
        EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
        EXPECT_EQ(tested.eventSink.characters.size(), 1u);
        // El error detiene el sufijo; no revierte los stores anteriores al callback.
        EXPECT_EQ(tested.session.CreateResult(),
                  deleting ? std::nullopt : std::optional<std::uint8_t>{std::uint8_t{1}});
        EXPECT_EQ(tested.session.DeleteResult(),
                  deleting ? std::optional<std::uint8_t>{std::uint8_t{1}} : std::nullopt);
        ASSERT_TRUE(tested.session.Characters().has_value());
        const auto& characters = tested.session.Characters()->characters;
        ASSERT_EQ(characters.size(), deleting ? 1u : 3u);
        EXPECT_EQ(characters[0].slot, deleting ? 3 : 0);
        EXPECT_EQ(characters[0].name, NameBytes(deleting ? "char0002" : "char0001"));
        if (deleting)
        {
            EXPECT_EQ(tested.session.ChosenCharacter(), nullptr); // Su entrada ya no existe.
        }
        else
        {
            EXPECT_EQ(characters[1].slot, 3);
            EXPECT_EQ(characters[1].name, NameBytes("char0002"));
            const auto& created = characters[2];
            EXPECT_EQ(created.slot, 1);
            EXPECT_EQ(created.name, NameBytes("char0003"));
            EXPECT_EQ(created.level, 1);
            EXPECT_EQ(created.flags, 0);
            EXPECT_EQ(created.classByte, 0x20);
            EXPECT_EQ(created.guildStatus, 0xff);
            EXPECT_TRUE(created.withoutEquipment);
            EXPECT_EQ(created.equipment, (std::array<std::byte, Client::Protocol::Character::EquipmentSize>{}));
            // Se vuelve a consultar: el alta puede haber realocado el vector.
            ASSERT_NE(tested.session.ChosenCharacter(), nullptr);
            EXPECT_EQ(tested.session.ChosenCharacter()->slot, 0);
            EXPECT_EQ(tested.session.ChosenCharacter()->name, NameBytes("char0001"));
        }
    }
}

// La fotografía final ya no contiene al borrado; el collector conserva lista, alta normalizada y baja.
TEST(ServerSessionTest, CharacterEventsDeliverListCreationAndDeletionPayloadsInOrder)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.session.ChooseCharacter(std::uint8_t{0}); // fuera de turno: no cambia el lugar enviado
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{0});

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();

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
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, TwoAcceptedIdenticalCreationsPublishTwoEvents)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {CreatedReply(1)};
        static_cast<void>(tested.session.Poll());
        ASSERT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    }

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
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

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.Failure(), SessionFailure::GameServerLost);
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
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
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 1u);
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {
        Join({CreatedReply(1), FromTheGameServer(LogoutResult(0), tested.gameServerSends), CreatedReply(2)})};

    static_cast<void>(tested.session.Poll());

    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
    ASSERT_EQ(events.size(), 1u);
    const auto* created = std::get_if<CharacterCreated>(&events[0]);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->entry.slot, 1);
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
    static_cast<void>(tested.session.Poll());

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
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_EQ(tested.session.Characters()->characters[1].name, NameBytes("char0003"));
    ASSERT_EQ(tested.eventSink.TakeCharacterEvents().size(), 2u);

    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(5)};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el personaje creado no se suma: el lugar 5 no está en la selección"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// Con 0 o 2, solo el resultado, para el aviso de la ventana; con otro, el original no hace nada y se
// queda esperando (Q-C114).
TEST(ServerSessionTest, ARefusedCreationKeepsItsResult)
{
    for (const int result : {0, 2})
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
        static_cast<void>(tested.eventSink.TakeCharacterEvents());
        tested.session.CreateCharacter("char0003", 1);
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, static_cast<unsigned char>(result)})};
        static_cast<void>(tested.session.Poll());
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.CreateResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    }
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x05})};
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.Logged(LogLevel::Warning, "el resultado 5 no lo atiende el original"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// La respuesta de crear fuera de turno se ignora.
TEST(ServerSessionTest, ACreateReplyOutOfTurnIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());

    tested.socket.reads = {CreatedReply(1)};
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(
        tested.Logged(LogLevel::Warning, "se ignora la respuesta de crear un personaje (F3 01): llegó fuera de turno"));
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, AMalformedCreationPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x01, 0x01})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    EXPECT_EQ(tested.session.CreateResult(), std::nullopt);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ASecondCreateReplyWithoutANewRequestPublishesNoEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {Join({CreatedReply(1), CreatedReply(1)})};

    static_cast<void>(tested.session.Poll());

    const std::vector<CharacterEvent> events = tested.eventSink.TakeCharacterEvents();
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
    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{std::uint8_t{1}});
    const std::vector<CharacterEntry>& characters = tested.session.Characters()->characters;
    ASSERT_EQ(characters.size(), 1u);
    EXPECT_EQ(characters[0].slot, 0);
    EXPECT_EQ(tested.session.ChosenCharacter(), nullptr);
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se borró el personaje del lugar 3"));
    tested.session.CreateCharacter("char0003", 1);
    tested.socket.reads = {CreatedReply(3)};
    static_cast<void>(tested.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x00})};
    static_cast<void>(tested.session.Poll());
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
        static_cast<void>(tested.eventSink.TakeCharacterEvents());
        tested.session.ChooseCharacter(std::uint8_t{3});
        tested.session.DeleteCharacter(SyntheticDeleteCode());
        tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, static_cast<unsigned char>(result)})};
        static_cast<void>(tested.session.Poll());
        EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter) << result;
        EXPECT_EQ(tested.session.DeleteResult(), std::optional<std::uint8_t>{static_cast<std::uint8_t>(result)});
        EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
        EXPECT_NE(tested.session.ChosenCharacter(), nullptr);
        EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
    }
}

TEST(ServerSessionTest, AMalformedDeletionPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.session.ChooseCharacter(std::uint8_t{3});
    tested.session.DeleteCharacter(SyntheticDeleteCode());
    tested.socket.reads = {Bytes({0xC1, 0x04, 0xF3, 0x02})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::WaitingDeleteResult);
    EXPECT_EQ(tested.session.DeleteResult(), std::nullopt);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

TEST(ServerSessionTest, ADeleteReplyOutOfTurnPublishesNoCharacterEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    static_cast<void>(tested.eventSink.TakeCharacterEvents());
    tested.socket.reads = {Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})};

    static_cast<void>(tested.session.Poll());

    EXPECT_EQ(tested.session.State(), SessionState::ChoosingCharacter);
    EXPECT_EQ(tested.session.Characters()->characters.size(), 2u);
    EXPECT_TRUE(tested.eventSink.TakeCharacterEvents().empty());
}

// Las dos respuestas se esperan con el tope de los Waiting*, y el keepalive sigue en los dos estados.
TEST(ServerSessionTest, CreatingAndDeletingHaveTheReplyTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.AdvanceTo(seconds{5});
    tested.session.CreateCharacter("char0003", 1);
    tested.AdvanceTo(seconds{5} + ReplyTimeout - milliseconds{1});
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCreateResult);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::NoReply);
    EXPECT_TRUE(tested.Logged(LogLevel::Error,
                              "el GameServer no contestó en 30 s: se esperaba el resultado de crear un personaje"));

    Harness deleting(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(deleting.ReachChoosingCharacter());
    deleting.session.ChooseCharacter(std::uint8_t{0});
    deleting.AdvanceTo(seconds{5});
    deleting.session.DeleteCharacter(SyntheticDeleteCode());
    deleting.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(deleting.session.Poll());
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
    static_cast<void>(tested.session.Poll());
    EXPECT_EQ(tested.session.State(), SessionState::WaitingCharacterList);
    tested.AdvanceTo(seconds{5} + ReplyTimeout);
    static_cast<void>(tested.session.Poll());

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

// Mientras no llega la respuesta del F3 03, el original deja el mosaico sin tope (06 § La entrada
// al mundo, paso 4): la entrada tampoco tiene uno.
TEST(ServerSessionTest, EnteringWorldHasNoTimeout)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingCharacter());
    tested.session.ChooseCharacter(std::uint8_t{0});
    tested.session.EnterWorld();

    tested.PollEachFrameUntil(ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);
}

// La entrada al mundo (el plan de la CL7, CL7.L1, "La sesión"): el sink que aplica la respuesta
// del F3 03 publica el 0x3d en su fila 9 (0x4c4854, 06, tabla de 0x4c3940). Lo que sigue de esa
// aplicación ya corre en InWorld, y el F3 10 que llega detrás en el mismo lote también lo ve.
TEST(ServerSessionTest, PublishingTheEntryFromTheSinkMovesToInWorld)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    std::optional<SessionState> afterPublishing;
    tested.eventSink.onWorld = [&afterPublishing](const WorldEvent& event,
                                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<CharacterSelectResult>(event))
        {
            EXPECT_FALSE(session.JoinedWorld());
            session.PublishWorldEntry();
            afterPublishing = session.State();
        }
        return {};
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    ASSERT_TRUE(tested.session.Poll().HasValue());

    EXPECT_EQ(afterPublishing, SessionState::InWorld);
    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_TRUE(tested.session.JoinedWorld());
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.connection.State(), LinkState::Open);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list", "entry", "inventory"}));
    EXPECT_EQ(tested.eventSink.worldStates,
              (std::vector<SessionState>{SessionState::EnteringWorld, SessionState::InWorld}));
    EXPECT_TRUE(tested.Logged(LogLevel::Info, "se aplicó la entrada al mundo: la sesión está en el mundo"));
}

// En el mundo no se espera una respuesta: no hay tope (ADR 0007), y el keepalive sigue con el timer
// global, que publicar no reinicia. A los 20 s, con la entrada publicada a los 5, ya salió uno: si
// la fase empezara al publicar, saldría a los 25.
TEST(ServerSessionTest, InWorldHasNoReplyTimeoutAndKeepsTheKeepalive)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    ASSERT_EQ(tested.Keepalives().size(), 1u); // el inmediato del login
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    tested.AdvanceTo(seconds{5});
    tested.socket.reads = {FromTheGameServer(WorldEntryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.session.State(), SessionState::InWorld);

    tested.PollEachFrameUntil(seconds{20});
    EXPECT_EQ(tested.Keepalives().size(), 2u);
    tested.PollEachFrameUntil(seconds{5} + ReplyTimeout * 3);

    EXPECT_EQ(tested.session.State(), SessionState::InWorld);
    EXPECT_FALSE(tested.session.Failure().has_value());
    EXPECT_EQ(tested.Keepalives().size(), 5u); // y a los 40, 60 y 80 s
    EXPECT_FALSE(tested.Logged(LogLevel::Error, "no contestó"));
}

// Lo que sigue al 0x3d en 0x4c3940 (de la fila 10 en adelante) también puede fallar en el
// aplicador: con InWorld ya publicado, la sesión termina en ApplicationFailed, cierra y corta el
// lote, y Poll devuelve el error hasta el próximo Start.
TEST(ServerSessionTest, AFailureAfterPublishingEndsInApplicationFailed)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        EXPECT_EQ(session.State(), SessionState::InWorld);
        return Client::Core::Unexpected(std::string("falló lo que sigue al 0x3d"));
    };
    tested.socket.reads = {Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                                 FromTheGameServer(InventoryReply(), tested.gameServerSends)})};

    const auto polled = tested.session.Poll();

    ASSERT_FALSE(polled.HasValue());
    EXPECT_EQ(polled.Error(), "falló lo que sigue al 0x3d");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
    EXPECT_FALSE(tested.session.JoinedWorld());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_EQ(tested.connection.State(), LinkState::Closed);
    EXPECT_EQ(tested.eventSink.trace, (std::vector<std::string>{"list", "entry"}));
    const auto again = tested.session.Poll();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), "falló lo que sigue al 0x3d");
    EXPECT_EQ(tested.session.State(), SessionState::Failed);
}

// Solo 0x4c3940 escribe el 0x3d: ni el F3 10 (0x4c55a0) ni nadie fuera de esa respuesta. Publicar
// desde otro lado es un aviso y nada, así tampoco saca a la sesión de QuitOrdered, que es el final.
TEST(ServerSessionTest, PublishingOutsideTheEntryApplicationIsIgnored)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachEnteringWorld());
    tested.eventSink.onWorld = [](const WorldEvent&,
                                  ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        session.PublishWorldEntry();
        return {};
    };
    tested.socket.reads = {FromTheGameServer(InventoryReply(), tested.gameServerSends)};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);

    tested.session.PublishWorldEntry();
    EXPECT_EQ(tested.session.State(), SessionState::EnteringWorld);

    tested.socket.reads = {FromTheGameServer(LogoutResult(0), tested.gameServerSends)};
    static_cast<void>(tested.session.Poll());
    ASSERT_EQ(tested.session.State(), SessionState::QuitOrdered);
    tested.session.PublishWorldEntry();

    EXPECT_EQ(tested.session.State(), SessionState::QuitOrdered);
    EXPECT_FALSE(tested.session.JoinedWorld());
    constexpr std::string_view Ignored = "se ignora la publicación de la entrada al mundo: solo la hace la respuesta "
                                         "del F3 03";
    EXPECT_EQ(tested.CountLogged(LogLevel::Warning, Ignored), 3u);
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
    static_cast<void>(tested.session.Poll());

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

// 04 § Caminar y pegar y cambios de mapa: el reparto nuevo no pierde los mensajes de un lote.
TEST(ServerSessionTest, WalkDamageTeleportAndRegenReachTheSinkInWireOrder)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto walk = Bytes({0xc1, 8, 0xd3, 0x12, 0x34, 7, 9, 0xf0});
    const auto damage = Bytes({0xc1, 8, 0xd7, 0x92, 0x34, 0x13, 0x57, 0xc1});
    const auto teleport = Bytes({0xc1, 8, 0x1c, 0x17, 3, 11, 12, 0});
    const auto regen =
        Bytes({0xc1, 24, 0xf3, 4, 21, 22, 2, 0, 0x34, 0x12, 0x78, 0x56, 0xbc, 0x9a, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8});
    tested.socket.reads = {Join({walk, damage, FromTheGameServer(teleport, tested.gameServerSends), regen})};
    EXPECT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.eventSink.world.size(), 4u);
    const auto* moved = std::get_if<Client::Protocol::Movement::Walk>(&tested.eventSink.world[0]);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->characterId, 0x1234);
    EXPECT_EQ(moved->cell.x, 7);
    EXPECT_EQ(moved->cell.y, 9);
    EXPECT_EQ(moved->direction, 15);
    const auto* hit = std::get_if<Client::Protocol::Movement::Damage>(&tested.eventSink.world[1]);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->characterId, 0x1234);
    EXPECT_EQ(hit->amount, 0x1357);
    EXPECT_TRUE(hit->shock);
    EXPECT_TRUE(hit->flag);
    EXPECT_TRUE(hit->doubleHit);
    EXPECT_EQ(hit->type, 1);
    const auto* warped = std::get_if<Client::Protocol::World::Teleport>(&tested.eventSink.world[2]);
    ASSERT_NE(warped, nullptr);
    EXPECT_EQ(warped->gate, 23);
    EXPECT_EQ(warped->map, 3);
    EXPECT_EQ(warped->direction, 0);
    const auto* revived = std::get_if<Client::Protocol::World::Regen>(&tested.eventSink.world[3]);
    ASSERT_NE(revived, nullptr);
    EXPECT_EQ(revived->x, 21);
    EXPECT_EQ(revived->map, 2);
    EXPECT_EQ(revived->life, 0x1234);
    EXPECT_EQ(revived->mana, 0x5678);
    EXPECT_EQ(revived->bp, 0x9abc);
    EXPECT_EQ(revived->experience, 0x04030201u);
    EXPECT_EQ(revived->zen, 0x08070605u);
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

// P-C33: incluso un 1C sin campos pasa por el rechazo antes del lector semántico.
TEST(ServerSessionTest, UnencryptedTeleportIsAnsweredWithF103BeforeReadingItsFields)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Bytes({0xc1, 3, 0x1c})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_FALSE(received.packets.empty());
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 6, 0xf1, 3, 0, 0x1c})));
    EXPECT_TRUE(received.packets.back().WasDecrypted());
    EXPECT_TRUE(tested.eventSink.world.empty());
}

// 04 § Mover y equipar ítems: el 24 cifrado, el 28, el 2A y los F3 14 y F3 13 llegan al mundo en orden; el 24 sin
// cifrar se contesta con el C1 06 F1 03 00 16 (0x4d166b) sin leerlo, y el 24 del cliente sale con XOR32 y en C3.
// El ítem es inventado.
TEST(ServerSessionTest, TheItemMovesReachTheWorldAndTheClearOneIsReported)
{
    namespace Inventory = Client::Protocol::Inventory;
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    auto equipment = Bytes({0xc1, 0x18, 0xf3, 0x13, 0x12, 0x34, 0});
    equipment.resize(0x18, std::byte{0x11});
    tested.socket.reads = {Join(
        {FromTheGameServer(Bytes({0xc1, 0x0c, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0}), tested.gameServerSends),
         Bytes({0xc1, 5, 0x28, 0x16, 1}), Bytes({0xc1, 6, 0x2a, 2, 9, 0}),
         Bytes({0xc1, 0x0c, 0xf3, 0x14, 0x17, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0}), equipment})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 5u);
    const auto* moved = std::get_if<Inventory::ItemMoveResult>(&events[0]);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->result, 0);
    EXPECT_EQ(moved->slot, 0x15);
    EXPECT_EQ(moved->item[0], std::byte{0x21});
    const auto* deleted = std::get_if<Inventory::ItemDelete>(&events[1]);
    ASSERT_NE(deleted, nullptr);
    EXPECT_EQ(deleted->slot, 0x16);
    const auto* durability = std::get_if<Inventory::Durability>(&events[2]);
    ASSERT_NE(durability, nullptr);
    EXPECT_EQ(durability->durability, 9);
    const auto* put = std::get_if<Inventory::ItemPut>(&events[3]);
    ASSERT_NE(put, nullptr);
    EXPECT_EQ(put->slot, 0x17);
    const auto* change = std::get_if<Inventory::EquipmentChange>(&events[4]);
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->key, 0x1234);

    const auto before = tested.GameServerReads().packets.size();
    tested.socket.reads = {Bytes({0xc1, 0x0c, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto reported = tested.GameServerReads();
    ASSERT_EQ(reported.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(reported.packets.back()).Bytes(), Bytes({0xc1, 6, 0xf1, 3, 0, 0x16})));
    EXPECT_TRUE(reported.packets.back().WasDecrypted());
    EXPECT_TRUE(tested.eventSink.world.empty());

    ASSERT_TRUE(tested.session
                    .SendItemMove(Inventory::ItemMove{
                        .sourceFlag = 0, .sourceSlot = 0x15, .item = moved->item, .targetFlag = 0, .targetSlot = 0x16})
                    .HasValue());
    const auto sent = tested.GameServerReads();
    ASSERT_EQ(sent.packets.size(), before + 2);
    EXPECT_TRUE(SameBytes(Xor32Decode(sent.packets.back()).Bytes(),
                          Bytes({0xc1, 0x0e, 0x24, 0, 0x15, 0x21, 0x0b, 0x2c, 0, 0, 0x70, 0, 0, 0x16})));
    EXPECT_TRUE(sent.packets.back().WasDecrypted());
}

// 04 § La vida y el maná: el 26 y el 27 llegan al mundo también antes del F3 03 (0x4d3510 y 0x4d3820 no miran la
// escena); el 26 corto con 0xff se ignora. Los valores son inventados.
TEST(ServerSessionTest, TheLifeAndTheManaReachTheWorldBeforeTheMap)
{
    namespace Inventory = Client::Protocol::Inventory;
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.socket.reads = {Join({Bytes({0xc1, 7, 0x26, 0xfe, 0x01, 0x2c, 0}), Bytes({0xc1, 5, 0x26, 0xff, 0x01}),
                                 Bytes({0xc1, 8, 0x27, 0x15, 0x00, 0x5a, 0, 0})})};
    ASSERT_TRUE(tested.session.Poll().HasValue());
    const auto events = tested.eventSink.TakeWorldEvents();
    ASSERT_EQ(events.size(), 2u);
    const auto* life = std::get_if<Inventory::LifeRefill>(&events[0]);
    ASSERT_NE(life, nullptr);
    EXPECT_EQ(life->place, Inventory::MaximumPlace);
    EXPECT_EQ(life->life, 0x12c);
    const auto* mana = std::get_if<Inventory::ManaRefill>(&events[1]);
    ASSERT_NE(mana, nullptr);
    EXPECT_EQ(mana->place, 0x15);
    EXPECT_EQ(mana->mana, 0x5a);
}

// La conexión cifra sólo el gate: los tres C1 de mundo restantes salen con XOR32 sin C3.
TEST(ServerSessionTest, GateRequestAndTeleportDoneUseTheirDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    constexpr std::array<Client::Protocol::Movement::Cell, 4> Path{
        {{0xb6, 0x73}, {0xb5, 0x73}, {0xb4, 0x74}, {0xb3, 0x74}}};
    ASSERT_TRUE(tested.session.SendWalk(Path, {0, 0}, 0.0f).HasValue());
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 4);
    EXPECT_TRUE(
        SameBytes(Xor32Decode(received.packets[before]).Bytes(), Bytes({0xc1, 8, 0xd3, 0xb6, 0x73, 0x73, 0x76, 0x70})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 1]).Bytes(), Bytes({0xc1, 6, 0x1c, 23, 0, 0})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 2]).Bytes(), Bytes({0xc1, 4, 0xf3, 0x12})));
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets[before + 3]).Bytes(), Bytes({0xc1, 4, 0xbd, 0})));
    EXPECT_FALSE(received.packets[before].WasDecrypted());
    EXPECT_TRUE(received.packets[before + 1].WasDecrypted());
    EXPECT_FALSE(received.packets[before + 2].WasDecrypted());
    EXPECT_FALSE(received.packets[before + 3].WasDecrypted());
}

TEST(ServerSessionTest, InvalidWalkDoesNotSendOrDisturbTheSession)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.socket.sent.size();
    EXPECT_FALSE(tested.session.SendWalk({}, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
    EXPECT_EQ(tested.session.State(), SessionState::LoggedIn);
}

TEST(ServerSessionTest, WorldRequestsNeverGoToTheConnectServer)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto before = tested.socket.sent.size();
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    constexpr std::array<Client::Protocol::Movement::Cell, 1> Path{{{1, 2}}};
    EXPECT_FALSE(tested.session.SendWalk(Path, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
    EXPECT_EQ(tested.session.State(), SessionState::ChoosingServer);
}

TEST(ServerSessionTest, TeleportHistoryPrecedesAdmissionAndRemainsAnOwnedReception)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    auto clear = Bytes({0xc1, 3, 0x1c});
    const auto accepted = Bytes({0xc1, 8, 0x1c, 23, 3, 11, 12, 0});
    tested.socket.reads = {Join({clear, FromTheGameServer(accepted, tested.gameServerSends)})};
    tested.eventSink.onWorld = [&accepted](const WorldEvent&,
                                           ServerSession& session) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(session.Teleports().size(), 2u);
        EXPECT_TRUE(SameBytes(session.Teleports().back(), accepted));
        return {};
    };
    ASSERT_TRUE(tested.session.Poll().HasValue());
    ASSERT_EQ(tested.session.Teleports().size(), 2u);
    clear[2] = std::byte{0xff};
    EXPECT_TRUE(SameBytes(tested.session.Teleports().front(), Bytes({0xc1, 3, 0x1c})));
    EXPECT_EQ(tested.eventSink.world.size(), 1u);
    tested.session.Start();
    EXPECT_EQ(tested.session.Teleports().size(), 2u);
}

// CheckApplication también corta el lote para las alternativas nuevas.
TEST(ServerSessionTest, AWorldApplicationFailureStopsTheMovementBatchAtTheSecondEvent)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.eventSink.onWorld = [](const WorldEvent& event, ServerSession&) -> Client::Core::Expected<void, std::string>
    {
        if (std::holds_alternative<Client::Protocol::Movement::Damage>(event))
        {
            return Client::Core::Unexpected(std::string("falló el daño después del movimiento"));
        }
        return {};
    };
    tested.socket.reads = {Join({Bytes({0xc1, 8, 0xd3, 0, 1, 2, 3, 0x70}), Bytes({0xc1, 8, 0xd7, 0, 1, 0, 2, 0}),
                                 FromTheGameServer(Bytes({0xc1, 8, 0x1c, 1, 2, 3, 4, 5}), tested.gameServerSends)})};
    const auto result = tested.session.Poll();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "falló el daño después del movimiento");
    ASSERT_EQ(tested.eventSink.world.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<Client::Protocol::Movement::Walk>(tested.eventSink.world[0]));
    EXPECT_TRUE(std::holds_alternative<Client::Protocol::Movement::Damage>(tested.eventSink.world[1]));
    EXPECT_TRUE(tested.session.Teleports().empty());
    EXPECT_EQ(tested.session.Failure(), SessionFailure::ApplicationFailed);
    EXPECT_FALSE(tested.session.Poll().HasValue());
    EXPECT_EQ(tested.eventSink.world.size(), 2u);
}

TEST(ServerSessionTest, MovementAndWarpPreserveTheirOrderAcrossEveryReadSplit)
{
    for (std::size_t split = 1;; ++split)
    {
        Harness tested(IdentityObfuscation::Shared());
        ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
        const auto sequence =
            Join({Bytes({0xc1, 8, 0xd3, 0, 1, 2, 3, 0xf0}),
                  FromTheGameServer(Bytes({0xc1, 8, 0x1c, 1, 2, 3, 4, 5}), tested.gameServerSends),
                  Bytes({0xc1, 8, 0xd7, 0, 1, 0, 2, 0x40}), Bytes({0xc1, 8, 0xd3, 0, 1, 4, 5, 0x70})});
        if (split >= sequence.size())
        {
            break;
        }
        tested.socket.reads = {ByteVector(sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(split))};
        ASSERT_TRUE(tested.session.Poll().HasValue());
        tested.socket.reads = {ByteVector(sequence.begin() + static_cast<std::ptrdiff_t>(split), sequence.end())};
        ASSERT_TRUE(tested.session.Poll().HasValue());
        ASSERT_EQ(tested.eventSink.world.size(), 4u) << split;
        EXPECT_TRUE(std::holds_alternative<Client::Protocol::Movement::Walk>(tested.eventSink.world[0]));
        EXPECT_TRUE(std::holds_alternative<Client::Protocol::World::Teleport>(tested.eventSink.world[1]));
        EXPECT_TRUE(std::holds_alternative<Client::Protocol::Movement::Damage>(tested.eventSink.world[2]));
        const auto* last = std::get_if<Client::Protocol::Movement::Walk>(&tested.eventSink.world[3]);
        ASSERT_NE(last, nullptr);
        EXPECT_EQ(last->cell.x, 4);
        EXPECT_EQ(last->direction, 7);
    }
}

TEST(ServerSessionTest, TruncatedMovementAndWarpDoNotReachTheSink)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    for (const int head : {0xd3, 0xd7, 0x1c})
    {
        for (int size = 3; size < 8; ++size)
        {
            auto packet = Bytes({0xc1, size, head});
            packet.resize(static_cast<std::size_t>(size), std::byte{0});
            tested.socket.reads = {FromTheGameServer(packet, tested.gameServerSends)};
            ASSERT_TRUE(tested.session.Poll().HasValue());
        }
    }
    for (int size = 4; size < 24; ++size)
    {
        auto packet = Bytes({0xc1, size, 0xf3, 4});
        packet.resize(static_cast<std::size_t>(size), std::byte{0});
        tested.socket.reads = {packet};
        ASSERT_TRUE(tested.session.Poll().HasValue());
    }
    EXPECT_TRUE(tested.eventSink.world.empty());
    EXPECT_EQ(tested.session.Teleports().size(), 5u);
    EXPECT_NE(tested.session.State(), SessionState::Failed);
}

TEST(ServerSessionTest, AClosedGameServerDoesNotQueueWorldRequests)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    tested.connection.Close();
    const auto before = tested.socket.sent.size();
    tested.session.RequestGate(23);
    tested.session.ConfirmTeleport();
    tested.session.RequestCrywolfState();
    constexpr std::array<Client::Protocol::Movement::Cell, 1> Path{{{1, 2}}};
    EXPECT_FALSE(tested.session.SendWalk(Path, {1, 2}, 0.0f).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
}

TEST(ServerSessionTest, FacingUsesTheWorldConnectionAndXorDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    ASSERT_TRUE(tested.session.SendFacing(0xf7).HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(Xor32Decode(received.packets.back()).Bytes(), Bytes({0xc1, 5, 0x18, 0xf7, 0x7a})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// 0x61c210 manda el F3 30 en claro con el XOR32, por el GS: no pasa por el cifrado del C3.
TEST(ServerSessionTest, OptionDataUsesTheWorldConnectionAndXorDelivery)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
    const auto before = tested.GameServerReads().packets.size();
    Client::Protocol::OptionData::OptionBody body{};
    body[0] = 0x21;
    body[10] = 0x0a;
    body[0x17] = 0x5c;
    ASSERT_TRUE(tested.session.SendOptionData(body).HasValue());
    const auto received = tested.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    const auto decoded = Xor32Decode(received.packets.back());
    const auto bytes = decoded.Bytes();
    ASSERT_EQ(bytes.size(), 0x1cu);
    EXPECT_TRUE(SameBytes(bytes.first(4), Bytes({0xc1, 0x1c, 0xf3, 0x30})));
    EXPECT_EQ(bytes[4], std::byte{0x21});
    EXPECT_EQ(bytes[14], std::byte{0x0a});
    EXPECT_EQ(bytes[0x1b], std::byte{0x5c});
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

// 0x6599a0(4) manda el C1 03 42 en claro, sin XOR32 ni cifrado, y solo con el socket abierto.
TEST(ServerSessionTest, PartyListRequestGoesClearToTheGameServerOnly)
{
    Harness tested(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto beforeChoosing = tested.socket.sent.size();
    tested.session.RequestPartyList();
    EXPECT_EQ(tested.socket.sent.size(), beforeChoosing);

    Harness inWorld(IdentityObfuscation::Shared());
    ASSERT_NO_FATAL_FAILURE(inWorld.ReachLoggedIn());
    const auto before = inWorld.GameServerReads().packets.size();
    inWorld.session.RequestPartyList();
    const auto received = inWorld.GameServerReads();
    ASSERT_EQ(received.packets.size(), before + 1);
    EXPECT_TRUE(SameBytes(received.packets.back().Bytes(), Bytes({0xc1, 3, 0x42})));
    EXPECT_FALSE(received.packets.back().WasDecrypted());
}

TEST(ServerSessionTest, FacingCannotGoToTheConnectServerOrAClosedConnection)
{
    Harness tested(IdentityObfuscation::Shared());
    EXPECT_FALSE(tested.session.SendFacing(3).HasValue());
    ASSERT_NO_FATAL_FAILURE(tested.ReachChoosingServer());
    const auto before = tested.socket.sent.size();
    EXPECT_FALSE(tested.session.SendFacing(3).HasValue());
    EXPECT_EQ(tested.socket.sent.size(), before);
}
} // namespace
