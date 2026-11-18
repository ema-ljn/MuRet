#include "Protocol/Friends.h"

#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Los amigos (CL9 hito 9; 04, "Los amigos"): C0..C4 que llegan y los cinco que manda la ventana, byte a byte. El
// GameServer 0.97 no tiene amigos: los bytes salen del ASM del cliente. Los nombres son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Protocol::ProtocolError;
using Client::Protocol::Friends::FriendRow;
using Client::Protocol::Friends::ReadFriendAddResult;
using Client::Protocol::Friends::ReadFriendDeleteResult;
using Client::Protocol::Friends::ReadFriendList;
using Client::Protocol::Friends::ReadFriendRequest;
using Client::Protocol::Friends::ReadFriendState;
using Client::Protocol::Friends::WriteFriendAdd;
using Client::Protocol::Friends::WriteFriendAnswer;
using Client::Protocol::Friends::WriteFriendDelete;
using Client::Protocol::Friends::WriteFriendListRequest;
using Client::Protocol::Friends::WriteFriendState;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// La clave del XOR32 (0x6b8648, 03 § El XOR32), escrita acá de nuevo para no comparar el código consigo mismo.
constexpr std::array<int, 32> Xor32Key{0xe7, 0x6d, 0x3a, 0x89, 0xbc, 0xb2, 0x9f, 0x73, 0x23, 0xa8, 0xfe,
                                       0xb6, 0x49, 0x5d, 0x39, 0x5d, 0x8a, 0xcb, 0x63, 0x8d, 0xea, 0x7d,
                                       0x2b, 0x5f, 0xc3, 0xb1, 0xe9, 0x83, 0x29, 0x51, 0xe8, 0x56};

// Un C1 con el XOR32 desde el byte 3: cada byte con el de la clave y el anterior ya cifrado.
std::vector<std::byte> Xor32Of(std::vector<std::byte> plain)
{
    for (std::size_t at = 3; at < plain.size(); ++at)
        plain[at] ^= static_cast<std::byte>(Xor32Key[at % Xor32Key.size()]) ^ plain[at - 1];
    return plain;
}

// El nombre en sus 10 bytes, con ceros detrás.
std::vector<std::byte> NameOf(std::string_view name)
{
    std::vector<std::byte> bytes(10, std::byte{0});
    for (std::size_t at = 0; at < name.size() && at < bytes.size(); ++at)
        bytes[at] = static_cast<std::byte>(name[at]);
    return bytes;
}

std::vector<std::byte> Joined(std::vector<std::byte> head, std::span<const std::byte> tail)
{
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

std::vector<std::byte> SentBytes(const Client::Protocol::Outgoing& sent)
{
    return {sent.packet.Bytes().begin(), sent.packet.Bytes().end()};
}

// 0x48ee60 caso 3, sin la lista: el C0 sale sin el XOR32.
TEST(FriendsTest, TheListRequestGoesInTheClear)
{
    const auto sent = WriteFriendListRequest();
    EXPECT_EQ(sent.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(SentBytes(sent), Bytes({0xc1, 0x03, 0xc0})));
}

// 0x49d0b0 con el 11: el nombre de la caja.
TEST(FriendsTest, TheAddCarriesTheNameWithTheXor32)
{
    const auto sent = WriteFriendAdd("Gorrion");
    EXPECT_EQ(sent.delivery, Delivery::Clear);
    const auto plain = Joined(Bytes({0xc1, 0x0d, 0xc1}), NameOf("Gorrion"));
    EXPECT_TRUE(SameBytes(SentBytes(sent), Xor32Of(plain)));
}

// 0x4a3b80 con dueño −1: sí (1) o no (0) y el que pidió.
TEST(FriendsTest, TheAnswerCarriesTheYesOrNoAndTheName)
{
    for (const std::uint8_t answer : {std::uint8_t{1}, std::uint8_t{0}})
    {
        SCOPED_TRACE(answer);
        const auto sent = WriteFriendAnswer(answer, "Calandria");
        EXPECT_EQ(sent.delivery, Delivery::Clear);
        const auto plain = Joined(Bytes({0xc1, 0x0e, 0xc2, answer}), NameOf("Calandria"));
        EXPECT_TRUE(SameBytes(SentBytes(sent), Xor32Of(plain)));
    }
}

// 0x49d0b0 con el 12 y 1.
TEST(FriendsTest, TheDeleteCarriesTheName)
{
    const auto sent = WriteFriendDelete("Tordo");
    EXPECT_EQ(sent.delivery, Delivery::Clear);
    const auto plain = Joined(Bytes({0xc1, 0x0d, 0xc3}), NameOf("Tordo"));
    EXPECT_TRUE(SameBytes(SentBytes(sent), Xor32Of(plain)));
}

// 0x4a2be0 (el 12 con 1) manda el 0; el botón del estado (0x4a3030) con +0xe0 en 1, el 1.
TEST(FriendsTest, TheStateCarriesOneByte)
{
    for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(state);
        const auto sent = WriteFriendState(state);
        EXPECT_EQ(sent.delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(SentBytes(sent), Xor32Of(Bytes({0xc1, 0x04, 0xc4, state}))));
    }
}

// Un nombre de 10 va sin '\0'; uno más largo se corta en 10 (strncpy).
TEST(FriendsTest, TheNameFillsTenBytesAtMost)
{
    const auto full = WriteFriendAdd("Golondrina");
    EXPECT_TRUE(SameBytes(SentBytes(full), Xor32Of(Joined(Bytes({0xc1, 0x0d, 0xc1}), NameOf("Golondrina")))));
    const auto longer = WriteFriendDelete("Golondrinas");
    EXPECT_TRUE(SameBytes(SentBytes(longer), Xor32Of(Joined(Bytes({0xc1, 0x0d, 0xc3}), NameOf("Golondrina")))));
}

// 0x4dc010: pkt[4] las cartas, pkt[5] el tope, pkt[6] cuántos y los amigos de 11 bytes.
TEST(FriendsTest, TheListReadsTheLettersAndTheFriends)
{
    auto bytes = Bytes({0xc2, 0x00, 0x1d, 0xc0, 0x03, 0x32, 0x02});
    bytes = Joined(bytes, NameOf("Zorzal"));
    bytes.push_back(std::byte{0x02});
    bytes = Joined(bytes, NameOf("Golondrina"));
    bytes.push_back(std::byte{0xff});
    const auto list = ReadFriendList(PacketOf(bytes));
    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().letters, 3);
    EXPECT_EQ(list.Value().maxLetters, 0x32);
    EXPECT_EQ(list.Value().friends, (std::vector<FriendRow>{{"Zorzal", 2}, {"Golondrina", 0xff}}));
}

TEST(FriendsTest, AnEmptyListHasNoFriends)
{
    const auto list = ReadFriendList(PacketOf(Bytes({0xc2, 0x00, 0x07, 0xc0, 0x00, 0x00, 0x00})));
    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(list.Value().friends.empty());
}

TEST(FriendsTest, TheListIsAC2AndHasAllItsRows)
{
    EXPECT_EQ(ErrorOf(ReadFriendList(PacketOf(Bytes({0xc1, 0x06, 0xc0, 0x00, 0x00, 0x00})))),
              ProtocolError::WrongPacket);
    auto bytes = Bytes({0xc2, 0x00, 0x12, 0xc0, 0x00, 0x00, 0x02});
    bytes = Joined(bytes, NameOf("Zorzal"));
    bytes.push_back(std::byte{0x01});
    EXPECT_EQ(ErrorOf(ReadFriendList(PacketOf(bytes))), ProtocolError::TooShort);
}

// 0x4dc3d0: pkt[3] el resultado, pkt[4..13] el nombre, pkt[0xe] el estado.
TEST(FriendsTest, TheAddResultReadsTheResultTheNameAndTheState)
{
    auto bytes = Joined(Bytes({0xc1, 0x0f, 0xc1, 0x01}), NameOf("Hornero"));
    bytes.push_back(std::byte{0x05});
    const auto result = ReadFriendAddResult(PacketOf(bytes));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 1);
    EXPECT_EQ(result.Value().name, "Hornero");
    EXPECT_EQ(result.Value().state, 5);
    EXPECT_EQ(ErrorOf(ReadFriendAddResult(PacketOf(Joined(Bytes({0xc1, 0x0e, 0xc1, 0x01}), NameOf("Hornero"))))),
              ProtocolError::TooShort);
}

// 0x4dc5b0: pkt[3..12].
TEST(FriendsTest, TheRequestReadsTheName)
{
    const auto request = ReadFriendRequest(PacketOf(Joined(Bytes({0xc1, 0x0d, 0xc2}), NameOf("Benteveo"))));
    ASSERT_TRUE(request.HasValue());
    EXPECT_EQ(request.Value().name, "Benteveo");
}

// 0x4dc690: pkt[3] y pkt[4..13].
TEST(FriendsTest, TheDeleteResultReadsTheResultAndTheName)
{
    const auto result = ReadFriendDeleteResult(PacketOf(Joined(Bytes({0xc1, 0x0e, 0xc3, 0x00}), NameOf("Tordo"))));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 0);
    EXPECT_EQ(result.Value().name, "Tordo");
}

// 0x4dc720: pkt[3..12] y pkt[0xd]. Un nombre de 10 no trae '\0'.
TEST(FriendsTest, TheStateReadsTheNameAndTheState)
{
    auto bytes = Joined(Bytes({0xc1, 0x0e, 0xc4}), NameOf("Golondrina"));
    bytes.push_back(std::byte{0xfc});
    const auto state = ReadFriendState(PacketOf(bytes));
    ASSERT_TRUE(state.HasValue());
    EXPECT_EQ(state.Value().name, "Golondrina");
    EXPECT_EQ(state.Value().state, 0xfc);
}
} // namespace
