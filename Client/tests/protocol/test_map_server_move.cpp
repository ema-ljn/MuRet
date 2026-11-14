#include "Protocol/Connection/MapServerMove.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

// El cambio de servidor del mapa (CL9 hito 13): el B1 00 que llega (0x4e5056 → 0x412aa0) y el B1 01 que sale
// (0x412bb0). El GameServer 0.97 no tiene ninguno: cada byte sale del ASM del cliente (*estático*). La cuenta y el
// personaje son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Packet;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ClientSerial;
using Client::Protocol::ClientVersion;
using Client::Protocol::Outgoing;
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::MapServerAuth;
using Client::Protocol::Connection::MapServerCodes;
using Client::Protocol::Connection::MapServerMove;
using Client::Protocol::Connection::ReadMapServerMove;
using Client::Protocol::Connection::WriteMapServerAuth;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// C1 28 B1 00, el host en 16 bytes, el puerto (abajo primero), dos bytes que nadie lee y los 16 de los códigos.
std::vector<std::byte> MovePacket(std::initializer_list<int> host)
{
    std::vector<std::byte> packet = Bytes({0xc1, 0x28, 0xb1, 0x00});
    std::vector<std::byte> hostBytes = Bytes(host);
    hostBytes.resize(16);
    packet.insert(packet.end(), hostBytes.begin(), hostBytes.end());
    const std::vector<std::byte> rest = Bytes({0xa5, 0xdb, 0xee, 0xff, // 56229 y los dos sin leer
                                               0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, //
                                               0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10});
    packet.insert(packet.end(), rest.begin(), rest.end());
    return packet;
}

MapServerMove Read(std::vector<std::byte> bytes)
{
    auto read = ReadMapServerMove(PacketOf(std::move(bytes)));
    EXPECT_TRUE(read.HasValue());
    return read.HasValue() ? std::move(read).Value() : MapServerMove{};
}

TEST(MapServerMoveTest, ReadsTheHostThePortAndTheCodes)
{
    const MapServerMove move = Read(MovePacket({'1', '0', '.', '0', '.', '0', '.', '7', 0, 'x', 'y'}));
    EXPECT_EQ(move.host, "10.0.0.7");
    EXPECT_TRUE(move.hostEnds);
    EXPECT_EQ(move.port, 56229);
    for (std::size_t i = 0; i < move.codes.size(); ++i)
    {
        EXPECT_EQ(std::to_integer<int>(move.codes.at(i)), static_cast<int>(i + 1)) << i;
    }
}

// Un host de 16 bytes sin 0 sigue en el puerto y los códigos, que son del mismo objeto (0x4c1340 lo toma como texto).
TEST(MapServerMoveTest, AHostWithoutZeroRunsIntoThePort)
{
    std::vector<std::byte> packet = MovePacket({'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', //
                                                'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'});
    packet[0x18] = std::byte{0};
    const MapServerMove move = Read(std::move(packet));
    EXPECT_EQ(move.host, std::string("abcdefghijklmnop\xa5\xdb\xee\xff"));
    EXPECT_TRUE(move.hostEnds);
}

// Sin ningún 0 en los 0x24 bytes, la lectura del original sale del objeto (Q-C1402).
TEST(MapServerMoveTest, NoZeroInThe24BytesLeavesTheHostOpen)
{
    std::vector<std::byte> packet = MovePacket({'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', //
                                                'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'});
    EXPECT_FALSE(Read(std::move(packet)).hostEnds);
}

TEST(MapServerMoveTest, ShortOrOtherSubcodeIsRejected)
{
    std::vector<std::byte> shortPacket = MovePacket({'h', 0});
    shortPacket.pop_back();
    shortPacket[1] = std::byte{0x27};
    EXPECT_EQ(ErrorOf(ReadMapServerMove(PacketOf(std::move(shortPacket)))), ProtocolError::TooShort);

    std::vector<std::byte> other = MovePacket({'h', 0});
    other[3] = std::byte{0x01};
    EXPECT_EQ(ErrorOf(ReadMapServerMove(PacketOf(std::move(other)))), ProtocolError::WrongPacket);

    EXPECT_EQ(ErrorOf(ReadMapServerMove(PacketOf(Bytes({0xc1, 3, 0xb1})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadMapServerMove(PacketOf(Bytes({0xc1, 4, 0xb2, 0})))), ProtocolError::WrongPacket);
}

// En un C2 el subcódigo es pkt[4], que también es el primer byte del host: solo un host vacío pasa.
TEST(MapServerMoveTest, InAC2TheSubcodeIsTheFirstHostByte)
{
    std::vector<std::byte> packet = MovePacket({0});
    packet.insert(packet.begin() + 1, std::byte{0});
    packet[2] = std::byte{0x29};
    packet[3] = std::byte{0xb1};
    packet[4] = std::byte{0x00};
    packet[0] = std::byte{0xc2};
    const MapServerMove move = Read(std::move(packet));
    EXPECT_EQ(move.host, "");
    // El puerto queda un byte más atrás: pkt+0x14 y pkt+0x15 del C2.
    EXPECT_EQ(move.port, 0xa500);
}

// B1 01 (0x412bb0): C1 45 B1 01, la cuenta con el XOR3 sobre sus 12 bytes, el nombre sin él, los códigos, el tick
// de abajo primero, la versión y el serial; con el XOR32 y para cifrar como C3.
TEST(MapServerMoveTest, WritesTheAuthWithEachFieldAtItsOffset)
{
    MapServerCodes codes{};
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        codes.at(i) = static_cast<std::byte>(0xf0 + i);
    }
    auto written =
        WriteMapServerAuth({.account = "cuenta7", .character = "Halcon", .codes = codes, .tick = 0x0a0b0c0d});
    ASSERT_TRUE(written.HasValue());
    const Outgoing auth = std::move(written).Value();
    EXPECT_EQ(auth.delivery, Delivery::Encrypted);
    const Packet plain = Xor32Decode(auth.packet);
    const std::span<const std::byte> bytes = plain.Bytes();
    ASSERT_EQ(bytes.size(), 0x45u);
    EXPECT_TRUE(SameBytes(bytes.first(4), Bytes({0xc1, 0x45, 0xb1, 0x01})));
    // "cuenta7" y cinco 0, con FC CF AB.
    EXPECT_TRUE(SameBytes(bytes.subspan(4, 12), Bytes({'c' ^ 0xfc, 'u' ^ 0xcf, 'e' ^ 0xab, 'n' ^ 0xfc, 't' ^ 0xcf,
                                                       'a' ^ 0xab, '7' ^ 0xfc, 0xcf, 0xab, 0xfc, 0xcf, 0xab})));
    EXPECT_TRUE(SameBytes(bytes.subspan(16, 12), Bytes({'H', 'a', 'l', 'c', 'o', 'n', 0, 0, 0, 0, 0, 0})));
    EXPECT_TRUE(SameBytes(bytes.subspan(28, 16), codes));
    EXPECT_TRUE(SameBytes(bytes.subspan(44, 4), Bytes({0x0d, 0x0c, 0x0b, 0x0a})));
    EXPECT_TRUE(SameBytes(bytes.subspan(48, 5), BytesOf(ClientVersion)));
    EXPECT_TRUE(SameBytes(bytes.subspan(53, 16), BytesOf(ClientSerial)));
}

// 0x412c1e corta el nombre en 10; la cuenta de más de 10 no entra en sus 12 sin pisar la pila.
TEST(MapServerMoveTest, CutsTheNameAtTenAndRejectsALongAccount)
{
    auto written = WriteMapServerAuth({.account = "abcdefghij", .character = "Nombrelargo1"});
    ASSERT_TRUE(written.HasValue());
    const Packet plain = Xor32Decode(std::move(written).Value().packet);
    EXPECT_TRUE(
        SameBytes(plain.Bytes().subspan(16, 12), Bytes({'N', 'o', 'm', 'b', 'r', 'e', 'l', 'a', 'r', 'g', 0, 0})));

    EXPECT_EQ(ErrorOf(WriteMapServerAuth({.account = "abcdefghijk", .character = "Halcon"})),
              ProtocolError::FieldTooLong);
}
} // namespace
