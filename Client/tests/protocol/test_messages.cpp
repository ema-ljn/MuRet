#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"

#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// 04 § El chat que llega (CL8 hito 3). Los nombres y los textos son inventados.
namespace
{
using Client::Protocol::ProtocolError;
using Client::Protocol::Messages::ReadChat;
using Client::Protocol::Messages::ReadKeyChat;
using Client::Protocol::Messages::ReadNotice;
using Client::Protocol::Messages::ReadWhisper;
using Client::Protocol::Messages::ReadWhisperResult;
using Client::Protocol::OptionData::ReadOptionData;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// Un C1 con el opcode, el nombre de 10 bytes y el texto que sigue.
std::vector<std::byte> Spoken(int opcode, std::string_view name, std::string_view text)
{
    std::vector<std::byte> bytes{std::byte{0xc1}, std::byte{0}, static_cast<std::byte>(opcode)};
    for (std::size_t i = 0; i < 10; ++i)
        bytes.push_back(i < name.size() ? static_cast<std::byte>(name[i]) : std::byte{0});
    for (const char letter : text)
        bytes.push_back(static_cast<std::byte>(letter));
    bytes[1] = static_cast<std::byte>(bytes.size());
    return bytes;
}

std::string NameOf(const Client::Protocol::Messages::SpokenText& spoken)
{
    std::string name;
    for (const std::byte value : spoken.name)
        name.push_back(static_cast<char>(value));
    return name;
}

TEST(MessagesProtocolTest, ReadsTheNameAndTheRawTextOfAChat)
{
    const auto chat = ReadChat(PacketOf(Spoken(0x00, "Zorzal", "buen dia")));
    ASSERT_TRUE(chat.HasValue());
    EXPECT_EQ(NameOf(chat.Value()), std::string("Zorzal\0\0\0\0", 10));
    EXPECT_EQ(chat.Value().text, "buen dia");
}

// 0x4c6070 copia a lo sumo 60 bytes desde pkt[0xd]: el lector guarda esos, con sus '\0'.
TEST(MessagesProtocolTest, KeepsAtMostSixtyBytesOfTextWithTheirZeros)
{
    std::string text(70, 'a');
    text[3] = '\0';
    const auto whisper = ReadWhisper(PacketOf(Spoken(0x02, "Benteveo", text)));
    ASSERT_TRUE(whisper.HasValue());
    EXPECT_EQ(whisper.Value().text.size(), 60u);
    EXPECT_EQ(whisper.Value().text[3], '\0');
}

// Q-C510: lo que no llegó no se lee; un paquete que termina en el nombre trae el texto vacío, y uno que termina antes
// trae el resto del nombre en '\0' (0x4c6070 en la escena 2 no lee el paquete: el 00 corto tiene que llegar).
TEST(MessagesProtocolTest, AChatThatEndsAfterTheNameHasNoText)
{
    const auto chat = ReadChat(PacketOf(Spoken(0x00, "Hornero", "")));
    ASSERT_TRUE(chat.HasValue());
    EXPECT_TRUE(chat.Value().text.empty());

    const auto cut = ReadChat(PacketOf(Bytes({0xc1, 0x05, 0x00, 'Z', 'o'})));
    ASSERT_TRUE(cut.HasValue());
    EXPECT_EQ(NameOf(cut.Value()), std::string("Zo\0\0\0\0\0\0\0\0", 10));
    EXPECT_TRUE(cut.Value().text.empty());
    EXPECT_TRUE(ReadWhisper(PacketOf(Bytes({0xc1, 0x03, 0x02}))).HasValue());
}

TEST(MessagesProtocolTest, OtherTypesAndOpcodesAreOtherPackets)
{
    EXPECT_EQ(ErrorOf(ReadChat(PacketOf(Spoken(0x02, "Tero", "hola")))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadWhisper(PacketOf(Bytes({0xc2, 0x00, 0x05, 0x02, 0x00})))), ProtocolError::WrongPacket);
}

// 0x4e3403: pkt[3] es el byte alto de la clave.
TEST(MessagesProtocolTest, ReadsTheKeyBigEndianAndTheTextToTheEnd)
{
    const auto chat = ReadKeyChat(PacketOf(Bytes({0xc1, 0x0a, 0x01, 0x12, 0x34, 'h', 'o', 'l', 'a', 0})));
    ASSERT_TRUE(chat.HasValue());
    EXPECT_EQ(chat.Value().key, 0x1234);
    EXPECT_EQ(chat.Value().text, std::string("hola\0", 5));
    EXPECT_EQ(ErrorOf(ReadKeyChat(PacketOf(Bytes({0xc1, 0x04, 0x01, 0x12})))), ProtocolError::TooShort);
}

TEST(MessagesProtocolTest, ReadsTheWhisperResult)
{
    const auto result = ReadWhisperResult(PacketOf(Bytes({0xc1, 0x04, 0x0c, 0x01})));
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value().result, 1);
    EXPECT_EQ(ErrorOf(ReadWhisperResult(PacketOf(Bytes({0xc1, 0x03, 0x0c})))), ProtocolError::TooShort);
}

// 0x4c667d..0x4c66a8: pkt[4], el WORD de pkt[6], el DWORD de pkt[8] y pkt[0xc], sin invertir bytes.
TEST(MessagesProtocolTest, ReadsTheNoticeFields)
{
    const auto notice = ReadNotice(PacketOf(
        Bytes({0xc1, 0x12, 0x0d, 0x0b, 0x03, 0x99, 0x34, 0x12, 0xe6, 0xdc, 0xc8, 0xff, 0x19, 'h', 'o', 'l', 'a', 0})));
    ASSERT_TRUE(notice.HasValue());
    EXPECT_EQ(notice.Value().type, 0x0b);
    EXPECT_EQ(notice.Value().byte4, 3);
    EXPECT_EQ(notice.Value().word6, 0x1234);
    EXPECT_EQ(notice.Value().color, 0xffc8dce6u);
    EXPECT_EQ(notice.Value().speed, 0x19);
    EXPECT_EQ(notice.Value().text, std::string("hola\0", 5));
    EXPECT_EQ(ErrorOf(ReadNotice(PacketOf(Bytes({0xc1, 0x05, 0x0d, 0x00, 0x00})))), ProtocolError::TooShort);
}

// C1 13 F3 30: las diez teclas, las banderas, Q, W y E y el registro del chat.
TEST(MessagesProtocolTest, ReadsTheOptionData)
{
    const auto data = ReadOptionData(PacketOf(Bytes({0xc1, 0x13, 0xf3, 0x30, 0x01, 0xff, 0x03, 0xff, 0xff, 0xff, 0xff,
                                                     0xff, 0xff, 0x0a, 0x15, 0x01, 0x04, 0x08, 0x24})));
    ASSERT_TRUE(data.HasValue());
    EXPECT_EQ(data.Value().skillKeys[0], 0x01);
    EXPECT_EQ(data.Value().skillKeys[2], 0x03);
    EXPECT_EQ(data.Value().skillKeys[9], 0x0a);
    EXPECT_EQ(data.Value().flags, 0x15);
    EXPECT_EQ(data.Value().quickSlots[0], 0x01);
    EXPECT_EQ(data.Value().quickSlots[2], 0x08);
    EXPECT_EQ(data.Value().chatWindow, 0x24);
    EXPECT_EQ(
        ErrorOf(ReadOptionData(PacketOf(Bytes({0xc1, 0x12, 0xf3, 0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})))),
        ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadOptionData(PacketOf(Bytes({0xc1, 0x04, 0xf3, 0x31})))), ProtocolError::WrongPacket);
}
} // namespace
