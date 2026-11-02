#include "Core/ByteReader.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"
#include "Network/Crypto/Xor32.h"
#include "Network/EncryptedPacket.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Hello.h"
#include "Protocol/Connection/Keepalive.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes del login contra el cable del laboratorio (support/WireCapture.h,
// MURET_CLIENT_WIRE_DIR): los C3 que mandó el cliente original, descifrados con Dec1.dat (la clave
// del servidor) y sin el XOR32, y lo que le contestaron el ConnectServer y el GameServer, leído con
// Protocol.
//
// El cable lleva el login del jugador: estos tests cuentan y miran cabeceras, opcodes, tamaños y la
// versión, y comparan con SameBytesWithoutValues. De la cuenta, la clave y el serial no se mira
// nada, y ningún mensaje imprime un byte del cable.
//
// El muproxy.log es del build histórico: su versión es 09908, y no la ClientVersion de este.
namespace
{
using Client::Core::ByteReader;
using Client::Network::DecryptPacket;
using Client::Network::FoxAntiObfuscation;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Protocol::VersionSize;
using Client::Protocol::Connection::HelloAccepted;
using Client::Protocol::Connection::LoginAccepted;
using Client::Protocol::Connection::ReadLoginResult;
using Client::Protocol::Connection::ReadServerAddress;
using Client::Protocol::Connection::ReadServerHello;
using Client::Protocol::Connection::ReadServerList;
using Client::Protocol::Connection::WriteKeepalive;
using Client::Protocol::Connection::WriteServerListRequest;
using Client::Protocol::Connection::WriteServerSelect;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::CutPackets;
using Client::Tests::ErrorOf;
using Client::Tests::LoadKeyFile;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::Unmasked;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using ByteVector = std::vector<std::byte>;

// El log que escribió el proxy del laboratorio.
constexpr std::string_view CaptureFile = "muproxy.log";

// Los puertos del proxy: delante del ConnectServer y del GameServer del laboratorio.
constexpr std::uint16_t ConnectServerPort = 44405;
constexpr std::uint16_t GameServerPort = 55901;

// La máscara de FoxAnti en todo lo que manda el GameServer del laboratorio: cada byte con XOR 0xAA.
constexpr std::byte FoxAntiMask{0xAA};

// Los opcodes y subcódigos que se buscan en el cable (04, "El flujo de conexión").
constexpr std::uint8_t ConnectOpcode = 0xF1;   // F1: saludo, login, cierre y aviso
constexpr std::uint8_t HelloSubcode = 0x00;    // F1 00, el saludo
constexpr std::uint8_t LoginSubcode = 0x01;    // F1 01, el login y su respuesta
constexpr std::uint8_t CloseSubcode = 0x02;    // F1 02, que el research del servidor llama cierre
constexpr std::uint8_t KeepaliveOpcode = 0x0E; // 0E, el keepalive
constexpr std::uint8_t ServerListOpcode = 0xF4;
constexpr std::uint8_t ListRequestSubcode = 0x02; // F4 02, pedir la lista, y la del laboratorio
constexpr std::uint8_t AddressSubcode = 0x03;     // F4 03, elegir y la respuesta

// Dónde está el subcódigo en un C1.
constexpr std::size_t SubcodeOffset = 3;

// Dónde están, en la lista, que es un C2, el subcódigo y la cantidad de servidores.
constexpr std::size_t ListSubcodeOffset = 4;
constexpr std::size_t ListCountOffset = 5;

// Lo que mide el login, dónde lleva la versión, y la del build del muproxy.log.
constexpr std::size_t LoginSize = 49;
constexpr std::size_t LoginVersionOffset = 28;
constexpr std::string_view CaptureVersion = "09908";

// Lo que mide un keepalive, y dónde lleva el tick; las dos velocidades van detrás.
constexpr std::size_t KeepaliveSize = 12;
constexpr std::size_t KeepaliveTickOffset = 4;

// Lo que mide el F1 02 que manda el cliente.
constexpr std::size_t CloseSize = 5;

// El GameServer al que manda el ConnectServer del laboratorio.
constexpr std::string_view GameServerHost = "172.16.198.1";

// El código que llega al ConnectServer en el F4 03: main.exe elige el 400 y FoxAnti le resta 400
// (04, "F4 03").
constexpr std::uint16_t WireServerCode = 0;

// El subcódigo de un C1 sin el XOR32, o std::nullopt si no tiene.
std::optional<std::uint8_t> SubcodeOf(const Packet& plain)
{
    if (plain.Bytes().size() <= SubcodeOffset)
    {
        return std::nullopt;
    }
    return std::to_integer<std::uint8_t>(plain.Bytes()[SubcodeOffset]);
}

// Si es la lista del ConnectServer del laboratorio: un C2 F4 02.
bool IsLabServerList(const Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C2 && packet.Opcode() == ServerListOpcode && bytes.size() > ListSubcodeOffset &&
           std::to_integer<std::uint8_t>(bytes[ListSubcodeOffset]) == ListRequestSubcode;
}

class GoldenLogin : public Client::Tests::WireDataTest
{
protected:
    void SetUp() override
    {
        WireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        const auto path = WireFile(CaptureFile);
        ASSERT_TRUE(path.has_value());
        const auto file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "no se pudo leer " << CaptureFile;
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": la línea " << parsed.Error().line << " no se entiende";
        m_connections = std::move(parsed).Value();
        m_dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        ASSERT_TRUE(m_dec1.has_value());
    }

    // Los paquetes de un sentido de la conexión `index`, con la máscara `mask` deshecha.
    std::vector<Packet> PacketsOf(std::size_t index, WireDirection direction, std::byte mask) const
    {
        const std::vector<ByteVector> unmasked = Unmasked(m_connections[index].Reads(direction), mask);
        const std::vector<std::span<const std::byte>> reads(unmasked.begin(), unmasked.end());
        return CutPackets(reads, index);
    }

    std::vector<WireConnection> m_connections;
    std::optional<DecryptionKey> m_dec1;
};

// Lo que se encontró en los C3 que mandó el cliente.
struct ClientCounts
{
    std::size_t f1 = 0;
    std::size_t logins = 0;
    std::size_t closes = 0;
    std::size_t otherF1 = 0;
    std::size_t keepalives = 0;
};

// Mira un login: 49 bytes, C1 31 F1 01 y la versión del build del muproxy.log en +28.
void CheckLogin(const Packet& plain, std::size_t index)
{
    const std::span<const std::byte> bytes = plain.Bytes();
    ASSERT_EQ(bytes.size(), LoginSize) << "conexión " << index;
    EXPECT_TRUE(SameBytesWithoutValues(bytes.first(4), Bytes({0xC1, 0x31, 0xF1, 0x01}))) << "conexión " << index;
    EXPECT_TRUE(SameBytesWithoutValues(bytes.subspan(LoginVersionOffset, VersionSize), BytesOf(CaptureVersion)))
        << "conexión " << index;
}

// Mira un keepalive: 12 bytes que empiezan con C1 0C 0E 00 y, con su tick y sus velocidades,
// WriteKeepalive da los mismos bytes que el cliente original, con el XOR32.
void CheckKeepalive(const Packet& encoded, const Packet& plain, std::size_t index)
{
    const std::span<const std::byte> bytes = plain.Bytes();
    ASSERT_EQ(bytes.size(), KeepaliveSize) << "conexión " << index;
    EXPECT_TRUE(SameBytesWithoutValues(bytes.first(4), Bytes({0xC1, 0x0C, 0x0E, 0x00}))) << "conexión " << index;
    ByteReader fields(bytes.subspan(KeepaliveTickOffset));
    const auto tick = fields.ReadU32Le();
    const auto attackSpeed = fields.ReadU16Le();
    const auto magicSpeed = fields.ReadU16Le();
    ASSERT_TRUE(tick && attackSpeed && magicSpeed) << "conexión " << index;
    const auto written = WriteKeepalive({*tick, *attackSpeed, *magicSpeed});
    EXPECT_TRUE(SameBytesWithoutValues(written.packet.Bytes(), encoded.Bytes())) << "conexión " << index;
}

// Los 183 C3 que mandó el cliente original al GameServer, descifrados y sin el XOR32: 9 son F1, y
// de ellos 7 son el login, de 49 bytes, y 2 el F1 02, de 5; 156 son el keepalive, de 12. El
// subcódigo del F1 viaja con el XOR32: se mira después de deshacerlo.
TEST_F(GoldenLogin, FindsTheLoginsAndKeepalivesThatTheClientSent)
{
    ClientCounts counts;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        PacketSerials serials;
        for (const Packet& packet : PacketsOf(index, WireDirection::ClientToServer, std::byte{0}))
        {
            if (packet.Type() != PacketType::C3)
            {
                continue;
            }
            const auto decrypted = DecryptPacket(packet, *m_dec1, serials);
            ASSERT_TRUE(decrypted.HasValue()) << "conexión " << index;
            const Packet& encoded = decrypted.Value().packet;
            const Packet plain = Xor32Decode(encoded);
            if (encoded.Opcode() == KeepaliveOpcode)
            {
                ++counts.keepalives;
                CheckKeepalive(encoded, plain, index);
                continue;
            }
            if (encoded.Opcode() != ConnectOpcode)
            {
                continue;
            }
            ++counts.f1;
            const std::optional<std::uint8_t> subcode = SubcodeOf(plain);
            if (subcode == LoginSubcode)
            {
                ++counts.logins;
                CheckLogin(plain, index);
            }
            else if (subcode == CloseSubcode)
            {
                ++counts.closes;
                EXPECT_EQ(plain.Bytes().size(), CloseSize) << "conexión " << index;
            }
            else
            {
                ++counts.otherF1;
            }
        }
    }

    EXPECT_EQ(counts.f1, 9u);
    EXPECT_EQ(counts.logins, 7u);
    EXPECT_EQ(counts.closes, 2u);
    EXPECT_EQ(counts.otherF1, 0u);
    EXPECT_EQ(counts.keepalives, 156u);
}

// Lo que contestó el GameServer, con el 0xAA deshecho a mano (hasta la CL2.4): en cada una de las 8
// conexiones lo primero es el saludo, que ReadServerHello lee con el resultado que acepta y la
// versión 09908, y los 7 logins se aceptan.
TEST_F(GoldenLogin, ReadsTheHellosAndLoginResultsOfTheGameServer)
{
    std::size_t connectionsWithAHelloFirst = 0;
    std::size_t hellos = 0;
    std::size_t accepted = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        const std::vector<Packet> packets = PacketsOf(index, WireDirection::ServerToClient, FoxAntiMask);
        if (!packets.empty() && ReadServerHello(packets.front()).HasValue())
        {
            ++connectionsWithAHelloFirst;
        }
        for (const Packet& packet : packets)
        {
            if (packet.Type() != PacketType::C1 || packet.Opcode() != ConnectOpcode)
            {
                continue;
            }
            if (SubcodeOf(packet) == HelloSubcode)
            {
                const auto hello = ReadServerHello(packet);
                ASSERT_TRUE(hello.HasValue()) << "conexión " << index;
                ++hellos;
                EXPECT_TRUE(hello.Value().result == HelloAccepted) << "conexión " << index;
                const std::string_view version(hello.Value().version.data(), hello.Value().version.size());
                EXPECT_TRUE(version == CaptureVersion) << "conexión " << index;
            }
            if (SubcodeOf(packet) == LoginSubcode)
            {
                const auto result = ReadLoginResult(packet);
                ASSERT_TRUE(result.HasValue()) << "conexión " << index;
                if (result.Value() == LoginAccepted)
                {
                    ++accepted;
                }
            }
        }
    }

    EXPECT_EQ(connectionsWithAHelloFirst, 8u);
    EXPECT_EQ(hellos, 8u);
    EXPECT_EQ(accepted, 7u);
}

// Con el ConnectServer, que va en claro: los 72 pedidos de la lista que mandó el cliente (en 8 de
// las 64 conexiones salió dos veces) son WriteServerListRequest byte a byte, las 8 elecciones F4 03
// son WriteServerSelect con el código que deja FoxAnti, y las 8 respuestas F4 03 mandan al
// GameServer del laboratorio, al puerto del proxy.
TEST_F(GoldenLogin, AsksForTheListChoosesAndReadsTheAddressLikeTheConnectServer)
{
    const auto request = WriteServerListRequest();
    const auto select = WriteServerSelect(WireServerCode);
    std::size_t requests = 0;
    std::size_t selects = 0;
    std::size_t addresses = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != ConnectServerPort)
        {
            continue;
        }
        for (const Packet& packet : PacketsOf(index, WireDirection::ClientToServer, std::byte{0}))
        {
            if (packet.Opcode() == ServerListOpcode && SubcodeOf(packet) == ListRequestSubcode)
            {
                ++requests;
                EXPECT_TRUE(SameBytesWithoutValues(packet.Bytes(), request.packet.Bytes())) << "conexión " << index;
            }
            if (packet.Opcode() == ServerListOpcode && SubcodeOf(packet) == AddressSubcode)
            {
                ++selects;
                EXPECT_TRUE(SameBytesWithoutValues(packet.Bytes(), select.packet.Bytes())) << "conexión " << index;
            }
        }
        for (const Packet& packet : PacketsOf(index, WireDirection::ServerToClient, std::byte{0}))
        {
            if (packet.Type() != PacketType::C1 || packet.Opcode() != ServerListOpcode ||
                SubcodeOf(packet) != AddressSubcode)
            {
                continue;
            }
            const auto address = ReadServerAddress(packet);
            ASSERT_TRUE(address.HasValue()) << "conexión " << index;
            ++addresses;
            EXPECT_TRUE(address.Value().host == GameServerHost) << "conexión " << index;
            EXPECT_EQ(address.Value().port, GameServerPort) << "conexión " << index;
        }
    }

    EXPECT_EQ(requests, 72u);
    EXPECT_EQ(selects, 8u);
    EXPECT_EQ(addresses, 8u);
}

// Las 72 listas del ConnectServer llegan como F4 02, y así ReadServerList no las lee; con el
// AfterCut de FoxAntiObfuscation (CL2.4) pasan a F4 06 y se leen todas, con las entradas que dice
// su cantidad.
TEST_F(GoldenLogin, ReadsTheListsOfTheConnectServerAfterFoxAnti)
{
    const FoxAntiObfuscation foxAnti;
    std::size_t lists = 0;
    std::size_t rejectedAsTheyArrive = 0;
    std::size_t readWithTheirCount = 0;
    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != ConnectServerPort)
        {
            continue;
        }
        for (Packet& packet : PacketsOf(index, WireDirection::ServerToClient, std::byte{0}))
        {
            if (!IsLabServerList(packet))
            {
                continue;
            }
            ++lists;
            if (ErrorOf(ReadServerList(packet)) == ProtocolError::WrongPacket)
            {
                ++rejectedAsTheyArrive;
            }
            const auto translated = foxAnti.AfterCut(std::move(packet));
            ASSERT_TRUE(translated.HasValue()) << "conexión " << index;
            const auto entries = ReadServerList(translated.Value());
            ASSERT_TRUE(entries.HasValue()) << "conexión " << index;
            ByteReader header(translated.Value().Bytes().subspan(ListCountOffset));
            const std::optional<std::uint16_t> count = header.ReadU16Be();
            if (count && entries.Value().size() == *count)
            {
                ++readWithTheirCount;
            }
        }
    }

    EXPECT_EQ(lists, 72u);
    EXPECT_EQ(rejectedAsTheyArrive, 72u);
    EXPECT_EQ(readWithTheirCount, 72u);
}
} // namespace
