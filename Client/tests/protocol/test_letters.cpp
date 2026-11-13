#include "Protocol/FriendChat.h"
#include "Protocol/Letters.h"

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
#include <span>
#include <string_view>
#include <vector>

// Las cartas y el chat con un amigo (CL9 hito 10; 04, "Las cartas y el chat con un amigo"), byte a byte. El GameServer
// 0.97 no los tiene: los bytes salen del ASM del cliente. Los nombres y los textos son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace Letters = Client::Protocol::Letters;
namespace Chat = Client::Protocol::FriendChat;

// La clave del XOR32 (0x6b8648), escrita acá de nuevo para no comparar el código consigo mismo.
constexpr std::array<int, 32> Xor32Key{0xe7, 0x6d, 0x3a, 0x89, 0xbc, 0xb2, 0x9f, 0x73, 0x23, 0xa8, 0xfe,
                                       0xb6, 0x49, 0x5d, 0x39, 0x5d, 0x8a, 0xcb, 0x63, 0x8d, 0xea, 0x7d,
                                       0x2b, 0x5f, 0xc3, 0xb1, 0xe9, 0x83, 0x29, 0x51, 0xe8, 0x56};

// El XOR32 desde `first` (3 en un C1, 4 en un C2).
std::vector<std::byte> Xor32Of(std::vector<std::byte> plain, std::size_t first = 3)
{
    for (std::size_t at = first; at < plain.size(); ++at)
        plain[at] ^= static_cast<std::byte>(Xor32Key[at % Xor32Key.size()]) ^ plain[at - 1];
    return plain;
}

std::vector<std::byte> TextOf(std::string_view text, std::size_t size)
{
    std::vector<std::byte> bytes(size, std::byte{0});
    for (std::size_t at = 0; at < text.size() && at < size; ++at)
        bytes[at] = static_cast<std::byte>(text[at]);
    return bytes;
}

// El XOR de 3 bytes del chat (0x6c82e4).
std::vector<std::byte> ChatTextOf(std::vector<std::byte> bytes)
{
    constexpr std::array<int, 3> Key{0xfc, 0xcf, 0xab};
    for (std::size_t at = 0; at < bytes.size(); ++at)
        bytes[at] ^= static_cast<std::byte>(Key[at % Key.size()]);
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

// 0x498820 con el 10 y 1: C2 C5 con el XOR32 desde el byte 4, cifrado como C4.
TEST(LettersTest, TheLetterGoesAsAnEncryptedC2)
{
    const Letters::LetterToSend letter{.window = 0x01020304,
                                       .name = "Calandria",
                                       .subject = "Hola desde el sur",
                                       .direction = 0x9e,
                                       .action = 0x02,
                                       .body = "Primer renglon\nSegundo"};
    const auto sent = Letters::WriteLetterSend(letter);
    EXPECT_EQ(sent.delivery, Delivery::Encrypted);
    auto plain = Bytes({0xc2, 0x00, 0x4c, 0xc5, 0x04, 0x03, 0x02, 0x01});
    plain = Joined(plain, TextOf("Calandria", 10));
    plain = Joined(plain, TextOf("Hola desde el sur", 0x20));
    plain = Joined(plain, Bytes({0x9e, 0x02, 0x16, 0x00}));
    plain = Joined(plain, TextOf("Primer renglon\nSegundo", 22));
    ASSERT_EQ(plain.size(), 0x4cU);
    EXPECT_TRUE(SameBytes(SentBytes(sent), Xor32Of(plain, 4)));
}

// El cuerpo llega cortado a 1000 (0x498ab4).
TEST(LettersTest, TheBodyIsCutAtAThousand)
{
    const Letters::LetterToSend letter{
        .window = 7, .name = "Tero", .subject = "Largo", .direction = 0, .action = 0, .body = std::string(1200, 'a')};
    const auto sent = Letters::WriteLetterSend(letter);
    ASSERT_EQ(sent.packet.Bytes().size(), 54U + 1000U);
}

// 0x4a11b0: el pedido de una carta (10 y 2), el borrado (12 y 1) y la lista (10 y 5).
TEST(LettersTest, TheLettersTabRequestsAreFixed)
{
    const auto read = Letters::WriteLetterReadRequest(0x0102);
    EXPECT_EQ(read.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(SentBytes(read), Xor32Of(Bytes({0xc1, 0x06, 0xc7, 0x00, 0x02, 0x01}))));
    const auto erase = Letters::WriteLetterDelete(0x0034);
    EXPECT_TRUE(SameBytes(SentBytes(erase), Xor32Of(Bytes({0xc1, 0x06, 0xc8, 0x00, 0x34, 0x00}))));
    EXPECT_TRUE(SameBytes(SentBytes(Letters::WriteLetterListRequest()), Bytes({0xc1, 0x03, 0xc9})));
}

// 0x4dc840: pkt[3] y pkt[4..7].
TEST(LettersTest, TheSendResultReadsTheResultAndTheWindow)
{
    const auto result = Letters::ReadLetterSendResult(PacketOf(Bytes({0xc1, 0x08, 0xc5, 0x01, 0x2a, 0, 0, 0})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 1);
    EXPECT_EQ(result.Value().window, 0x2aU);
    EXPECT_EQ(ErrorOf(Letters::ReadLetterSendResult(PacketOf(Bytes({0xc1, 0x07, 0xc5, 0x01, 0x2a, 0, 0})))),
              ProtocolError::TooShort);
}

// 0x4dc9e0: el renglón de la lista, por su lugar.
TEST(LettersTest, TheRowReadsEachFieldAtItsPlace)
{
    std::vector<std::byte> bytes(0x4f, std::byte{0});
    bytes[0] = std::byte{0xc1};
    bytes[1] = std::byte{0x4f};
    bytes[2] = std::byte{0xc6};
    bytes[4] = std::byte{0x0a};
    bytes[5] = std::byte{0x01};
    const auto put = [&bytes](std::size_t at, std::string_view text)
    {
        for (std::size_t i = 0; i < text.size(); ++i)
            bytes[at + i] = static_cast<std::byte>(text[i]);
    };
    put(6, "Benteveo");
    put(0x10, "2026-11-13 18:30:05");
    put(0x2e, "Un asunto inventado");
    bytes[0x4e] = std::byte{0x02};
    const auto row = Letters::ReadLetterRow(PacketOf(bytes));
    ASSERT_TRUE(row.HasValue());
    EXPECT_EQ(row.Value().index, 0x010a);
    EXPECT_EQ(row.Value().name, "Benteveo");
    EXPECT_EQ(row.Value().date, "2026-11-13");
    EXPECT_EQ(row.Value().time, "18:30:05");
    EXPECT_EQ(row.Value().subject, "Un asunto inventado");
    EXPECT_EQ(row.Value().type, 2);
}

// 0x4dcb70: un C2 con el retrato y el cuerpo, que se corta en el largo.
TEST(LettersTest, TheLetterReadsThePortraitAndTheBody)
{
    auto bytes = Bytes({0xc2, 0x00, 0x00, 0xc7, 0x05, 0x00, 0x05, 0x00, 0x21});
    for (int at = 0; at < 17; ++at)
        bytes.push_back(static_cast<std::byte>(0x40 + at));
    bytes = Joined(bytes, Bytes({0x85, 0x02}));
    bytes = Joined(bytes, TextOf("Chau!sobra", 10));
    bytes[2] = static_cast<std::byte>(bytes.size());
    const auto letter = Letters::ReadLetterRead(PacketOf(bytes));
    ASSERT_TRUE(letter.HasValue());
    EXPECT_EQ(letter.Value().index, 5);
    EXPECT_EQ(letter.Value().length, 5);
    EXPECT_EQ(letter.Value().portraitClass, 0x21);
    EXPECT_EQ(letter.Value().equipment[0], std::byte{0x40});
    EXPECT_EQ(letter.Value().equipment[16], std::byte{0x50});
    EXPECT_EQ(letter.Value().direction, 0x85);
    EXPECT_EQ(letter.Value().action, 0x02);
    EXPECT_EQ(letter.Value().body, "Chau!");
    ASSERT_EQ(letter.Value().copy.size(), bytes.size());
    EXPECT_EQ(letter.Value().copy[0x1c + 5], std::byte{0});
}

// Q-C1000: un largo que no entra en el paquete no se lee.
TEST(LettersTest, ALengthPastThePacketIsRejected)
{
    auto bytes = Bytes({0xc2, 0x00, 0x1e, 0xc7, 0x05, 0x00, 0x02, 0x00});
    bytes.resize(0x1e, std::byte{0});
    EXPECT_EQ(ErrorOf(Letters::ReadLetterRead(PacketOf(bytes))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(Letters::ReadLetterRead(PacketOf(Bytes({0xc1, 0x04, 0xc7, 0x00})))), ProtocolError::WrongPacket);
}

// 0x4dcd90: pkt[3] y pkt[4..5].
TEST(LettersTest, TheDeleteResultReadsTheResultAndTheIndex)
{
    const auto result = Letters::ReadLetterDeleteResult(PacketOf(Bytes({0xc1, 0x06, 0xc8, 0x01, 0x34, 0x12})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 1);
    EXPECT_EQ(result.Value().index, 0x1234);
}

// 0x49d0b0 con el 3 y 0x4944c0 con el 10 y 2: CA y CB por el GameServer.
TEST(FriendChatTest, TheRoomRequestAndTheInviteCarryTheName)
{
    const auto request = Chat::WriteRoomRequest("Hornero");
    EXPECT_EQ(request.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(SentBytes(request), Xor32Of(Joined(Bytes({0xc1, 0x0d, 0xca}), TextOf("Hornero", 10)))));
    const auto invite = Chat::WriteInvite("Zorzal", 0x0304, 0x0000002b);
    auto plain = Joined(Bytes({0xc1, 0x14, 0xcb}), TextOf("Zorzal", 10));
    plain = Joined(plain, Bytes({0x00, 0x04, 0x03, 0x2b, 0x00, 0x00, 0x00}));
    EXPECT_TRUE(SameBytes(SentBytes(invite), Xor32Of(plain)));
}

// 0x493530: el ticket en decimal, con el XOR de 3 bytes sobre sus 10.
TEST(FriendChatTest, TheJoinCarriesTheRoomAndTheTicket)
{
    const auto join = Chat::WriteJoin(0x0102, 4567);
    auto plain = Bytes({0xc1, 0x10, 0x00, 0x00, 0x02, 0x01});
    plain = Joined(plain, ChatTextOf(TextOf("4567", 10)));
    EXPECT_TRUE(SameBytes(SentBytes(join), Xor32Of(plain)));
}

// 0x4944c0 con el 7 y 0x493a70.
TEST(FriendChatTest, TheMessageAndTheLeave)
{
    const auto message = Chat::WriteMessage("hola che");
    auto plain = Bytes({0xc1, 0x0d, 0x04, 0x00, 0x08});
    plain = Joined(plain, ChatTextOf(TextOf("hola che", 8)));
    EXPECT_TRUE(SameBytes(SentBytes(message), Xor32Of(plain)));
    EXPECT_TRUE(SameBytes(SentBytes(Chat::WriteLeave()), Bytes({0xc1, 0x03, 0x01})));
}

// 0x4dce10: CA por su lugar.
TEST(FriendChatTest, TheRoomAnswerReadsEachField)
{
    std::vector<std::byte> bytes(0x24, std::byte{0});
    bytes[0] = std::byte{0xc1};
    bytes[1] = std::byte{0x24};
    bytes[2] = std::byte{0xca};
    const std::string_view address = "10.0.0.7";
    for (std::size_t at = 0; at < address.size(); ++at)
        bytes[3 + at] = static_cast<std::byte>(address[at]);
    bytes[0x12] = std::byte{0x09};
    bytes[0x14] = std::byte{0x11};
    bytes[0x15] = std::byte{0x22};
    bytes[0x18] = std::byte{0x01};
    const std::string_view name = "Calandria";
    for (std::size_t at = 0; at < name.size(); ++at)
        bytes[0x19 + at] = static_cast<std::byte>(name[at]);
    bytes[0x23] = std::byte{0x01};
    const auto answer = Chat::ReadRoomAnswer(PacketOf(bytes));
    ASSERT_TRUE(answer.HasValue());
    EXPECT_EQ(answer.Value().address, "10.0.0.7");
    EXPECT_EQ(answer.Value().room, 9);
    EXPECT_EQ(answer.Value().ticket, 0x2211U);
    EXPECT_EQ(answer.Value().type, 1);
    EXPECT_EQ(answer.Value().name, "Calandria");
    EXPECT_EQ(answer.Value().result, 1);
    bytes.pop_back();
    bytes[1] = std::byte{0x23};
    EXPECT_EQ(ErrorOf(Chat::ReadRoomAnswer(PacketOf(bytes))), ProtocolError::TooShort);
}

// 0x4dd1e0, 0x4dd350, 0x4dd3d0 y 0x4dd540: lo que llega por la conexión de la sala.
TEST(FriendChatTest, TheRoomPacketsReadByPlace)
{
    const auto member = Chat::ReadMember(PacketOf(Joined(Bytes({0xc1, 0x0f, 0x01, 0x01, 0x02}), TextOf("Tero", 10))));
    ASSERT_TRUE(member.HasValue());
    EXPECT_EQ(member.Value().kind, 1);
    EXPECT_EQ(member.Value().index, 2);
    EXPECT_EQ(member.Value().name, "Tero");

    auto list = Bytes({0xc1, 0x1e, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00});
    list = Joined(list, TextOf("Tero", 10));
    list.push_back(std::byte{0x01});
    list = Joined(list, TextOf("Calandria", 10));
    const auto members = Chat::ReadMembers(PacketOf(list));
    ASSERT_TRUE(members.HasValue());
    EXPECT_EQ(members.Value().members, (std::vector<Chat::MemberRow>{{0, "Tero"}, {1, "Calandria"}}));

    auto said = Bytes({0xc1, 0x0a, 0x04, 0x03, 0x05});
    said = Joined(said, ChatTextOf(TextOf("buena", 5)));
    const auto message = Chat::ReadMessage(PacketOf(said));
    ASSERT_TRUE(message.HasValue());
    EXPECT_EQ(message.Value().index, 3);
    EXPECT_EQ(message.Value().text, "buena");
    const auto tooLong = Chat::ReadMessage(PacketOf(Bytes({0xc1, 0x05, 0x04, 0x03, 0x65})));
    ASSERT_TRUE(tooLong.HasValue());
    EXPECT_EQ(tooLong.Value().length, 0x65);
    EXPECT_TRUE(tooLong.Value().text.empty());

    std::vector<std::byte> notice(0x69, std::byte{'x'});
    notice[0] = std::byte{0xc1};
    notice[1] = std::byte{0x69};
    notice[2] = std::byte{0x0d};
    const auto read = Chat::ReadNotice(PacketOf(notice));
    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().text, std::string(0x63, 'x'));
}
} // namespace
