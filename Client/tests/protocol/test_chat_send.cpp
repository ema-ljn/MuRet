#include "Protocol/Chat/ChatSend.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

// El C1 00 y el C1 02 de la entrada del chat (CL8 hito 3; 04, "El chat"). Los nombres y los textos son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Chat::ChatNameSize;
using Client::Protocol::Chat::ChatTextBytes;
using Client::Protocol::Chat::ChatTextLimit;
using Client::Protocol::Chat::WriteChat;
using Client::Protocol::Chat::WriteWhisper;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

// "Abc" y su '\0', con lo que quedó detrás en el registro: va tal cual.
constexpr std::array<std::byte, ChatNameSize> Name{std::byte{'A'}, std::byte{'b'}, std::byte{'c'}, std::byte{0},
                                                   std::byte{'z'}, std::byte{0},   std::byte{0},   std::byte{0},
                                                   std::byte{0},   std::byte{0}};

std::vector<std::byte> NameThen(std::initializer_list<int> tail)
{
    std::vector<std::byte> bytes(Name.begin(), Name.end());
    for (const int value : tail)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

std::vector<std::byte> PlainOf(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Xor32Decode(sent.packet);
    return {plain.Bytes().begin(), plain.Bytes().end()};
}

TEST(ChatSendTest, TheChatCarriesTheNameAndTheTextWithItsZero)
{
    const auto sent = WriteChat(Name, "hola");
    EXPECT_EQ(sent.delivery, Delivery::Clear);
    auto expected = Bytes({0xc1, 0x12, 0x00});
    const auto tail = NameThen({'h', 'o', 'l', 'a', 0});
    expected.insert(expected.end(), tail.begin(), tail.end());
    EXPECT_TRUE(SameBytes(PlainOf(sent), expected));
    // Con el XOR32 desde el byte 3: lo que sale no es lo de arriba.
    EXPECT_NE(sent.packet.Bytes()[3], expected[3]);
}

TEST(ChatSendTest, TheWhisperIsTheSameWithItsOpcode)
{
    const auto sent = WriteWhisper(Name, "ey");
    auto expected = Bytes({0xc1, 0x10, 0x02});
    const auto tail = NameThen({'e', 'y', 0});
    expected.insert(expected.end(), tail.begin(), tail.end());
    EXPECT_TRUE(SameBytes(PlainOf(sent), expected));
}

// 0x4b7aaf: con menos de 0x3c bytes va el '\0'; con 0x3c o más, los primeros 0x3c, sin él.
TEST(ChatSendTest, TheTextIsCutAtSixtyBytes)
{
    const std::string fiftyNine(59, 'x');
    const std::string sixty(60, 'y');
    const std::string seventy(70, 'w');
    EXPECT_EQ(ChatTextBytes(fiftyNine), ChatTextLimit);
    EXPECT_EQ(ChatTextBytes(sixty), ChatTextLimit);
    EXPECT_EQ(ChatTextBytes(seventy), ChatTextLimit);

    const auto withZero = PlainOf(WriteChat(Name, fiftyNine));
    ASSERT_EQ(withZero.size(), 0xd + ChatTextLimit);
    EXPECT_EQ(withZero.back(), std::byte{0});
    const auto cut = PlainOf(WriteChat(Name, seventy));
    ASSERT_EQ(cut.size(), 0xd + ChatTextLimit);
    EXPECT_EQ(cut[1], static_cast<std::byte>(0xd + ChatTextLimit));
    EXPECT_EQ(cut.back(), std::byte{'w'});
}

// Lo que sigue a un '\0' del texto no va: lo copia strcpy.
TEST(ChatSendTest, TheTextEndsAtItsZero)
{
    const std::string text("ab\0cd", 5);
    EXPECT_EQ(ChatTextBytes(text), 3u);
    const auto plain = PlainOf(WriteChat(Name, text));
    ASSERT_EQ(plain.size(), 0xdu + 3u);
    EXPECT_EQ(plain[0xd + 2], std::byte{0});
}
} // namespace
