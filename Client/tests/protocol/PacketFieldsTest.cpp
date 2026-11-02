// Client::Protocol::FieldsAfterSubcode y TextField: qué paquete es, y los textos de largo fijo.

#include "Protocol/PacketFields.h"

#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using Client::Network::Packet;
using Client::Network::PacketType;
using Client::Protocol::FieldsAfterSubcode;
using Client::Protocol::PacketKind;
using Client::Protocol::ProtocolError;
using Client::Protocol::TextField;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// El saludo del GameServer, F1 00, y la lista que deja FoxAnti, F4 06.
constexpr PacketKind Hello{PacketType::C1, 0xF1, 0x00};
constexpr PacketKind List{PacketType::C2, 0xF4, 0x06};

// Si FieldsAfterSubcode acepta este argumento. Como con el constructor de ByteReader, un Packet
// temporal no compila: el lector quedaría mirando los bytes de un paquete muerto.
template <typename Argument>
constexpr bool TakesThePacket = requires { FieldsAfterSubcode(std::declval<Argument>(), Hello); };

static_assert(TakesThePacket<const Packet&>);
static_assert(TakesThePacket<Packet&>);
static_assert(!TakesThePacket<Packet>);
static_assert(!TakesThePacket<const Packet>);

// El error de FieldsAfterSubcode con un paquete de estos bytes, que tiene nombre por lo de arriba.
std::optional<ProtocolError> FieldsErrorOf(std::vector<std::byte> bytes, const PacketKind& kind)
{
    const Packet packet = PacketOf(std::move(bytes));
    return ErrorOf(FieldsAfterSubcode(packet, kind));
}

// En un C1 el subcódigo es pkt[3], y los campos empiezan en pkt[4].
TEST(PacketFieldsTest, ReadsWhatFollowsTheSubcodeOfAC1)
{
    const auto packet = PacketOf(Bytes({0xC1, 0x06, 0xF1, 0x00, 0x01, 0x02}));

    auto fields = FieldsAfterSubcode(packet, Hello);

    ASSERT_TRUE(fields.HasValue());
    EXPECT_EQ(fields.Value().Remaining(), 2u);
    EXPECT_EQ(fields.Value().ReadU8(), std::uint8_t{0x01});
}

// En un C2, pkt[4], después del tamaño en dos bytes y del opcode.
TEST(PacketFieldsTest, ReadsWhatFollowsTheSubcodeOfAC2)
{
    const auto packet = PacketOf(Bytes({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x01}));

    auto fields = FieldsAfterSubcode(packet, List);

    ASSERT_TRUE(fields.HasValue());
    EXPECT_EQ(fields.Value().ReadU16Be(), std::uint16_t{1});
}

// Otro tipo, otro opcode u otro subcódigo es otro paquete. Un C3 trae el opcode cifrado: se lee
// después de DecryptPacket, que lo rearma como C1.
TEST(PacketFieldsTest, AnotherTypeOpcodeOrSubcodeIsWrongPacket)
{
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC2, 0x00, 0x05, 0xF1, 0x00}), Hello), ProtocolError::WrongPacket);
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC1, 0x04, 0xF3, 0x00}), Hello), ProtocolError::WrongPacket);
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC1, 0x04, 0xF1, 0x01}), Hello), ProtocolError::WrongPacket);
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC3, 0x04, 0xF1, 0x00}), Hello), ProtocolError::WrongPacket);
}

// El tipo se mira aparte: este C1 tiene el opcode F4 y un 06 en pkt[4], donde un C2 lleva el
// subcódigo, y no es la lista.
TEST(PacketFieldsTest, TheTypeIsCheckedEvenIfTheSubcodeWouldFit)
{
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC1, 0x05, 0xF4, 0x03, 0x06}), List), ProtocolError::WrongPacket);
}

// Con el opcode y sin el subcódigo, el paquete es corto: no se sabe cuál de los F1 es.
TEST(PacketFieldsTest, APacketThatEndsBeforeTheSubcodeIsTooShort)
{
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC1, 0x03, 0xF1}), Hello), ProtocolError::TooShort);
    EXPECT_EQ(FieldsErrorOf(Bytes({0xC2, 0x00, 0x04, 0xF4}), List), ProtocolError::TooShort);
}

// Un texto llega hasta su primer \0, y lo que sigue no se lee.
TEST(PacketFieldsTest, ATextEndsAtItsFirstNul)
{
    EXPECT_EQ(TextField(Bytes({0x31, 0x30, 0x00, 0x39, 0x39})), "10");
    EXPECT_EQ(TextField(Bytes({0x00, 0x31})), "");
}

// Q-C07: un campo lleno no tiene \0, y el texto son todos sus bytes, sin seguir en los de al lado.
TEST(PacketFieldsTest, AFullTextHasNoNul)
{
    EXPECT_EQ(TextField(Bytes({0x31, 0x30, 0x32, 0x30, 0x39})), "10209");
    EXPECT_EQ(TextField({}), "");
}

// Los bytes de 0x80 en adelante pasan como están, también el 0xFF: el texto no se interpreta, y
// solo el \0 lo termina.
TEST(PacketFieldsTest, ATextKeepsItsHighBytes)
{
    const std::string text = TextField(Bytes({0xE1, 0x7F, 0x80}));

    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(static_cast<unsigned char>(text[0]), 0xE1);
    EXPECT_EQ(static_cast<unsigned char>(text[2]), 0x80);
    EXPECT_EQ(TextField(Bytes({0xFF, 0x31, 0xFF})).size(), 3u);
}

} // namespace
