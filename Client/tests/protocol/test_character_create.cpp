#include "Protocol/Character/CharacterCreate.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterName.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>

// Client::Protocol::Character, crear un personaje (F3 01: 0x63d990 y 0x4c2290), con nombres
// sintéticos.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Protocol::Character::CharacterClassOf;
using Client::Protocol::Character::CharacterCreated;
using Client::Protocol::Character::NameAsText;
using Client::Protocol::Character::NameFromText;
using Client::Protocol::Character::NameUpToItsEnd;
using Client::Protocol::Character::ReadCharacterCreateResult;
using Client::Protocol::Character::WriteCharacterCreate;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::NameBytes;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// C1 0F F3 01, el nombre con ceros hasta 10 y la clase por 16 (la piel en 0), con el XOR32 desde el
// byte 3 y en claro.
TEST(CharacterCreateTest, TheRequestIsC10FF301WithTheNameAndTheClass)
{
    const auto create = WriteCharacterCreate(NameBytes("char0001"), 1);

    EXPECT_EQ(create.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(create.packet).Bytes(),
                          Bytes({0xC1, 0x0F, 0xF3, 0x01, 'c', 'h', 'a', 'r', '0', '0', '0', '1', 0x00, 0x00, 0x10})));
    EXPECT_FALSE(SameBytes(create.packet.Bytes(), Xor32Decode(create.packet).Bytes())) << "salió sin el XOR32";
    // El Dark Lord, la clase 4: 0x40.
    EXPECT_EQ(Xor32Decode(WriteCharacterCreate(NameBytes("char0001"), 4).packet).Bytes()[14], std::byte{0x40});
}

// El nombre va hasta su primer cero, y lo de después en 0 (el strlen de 0x63d990).
TEST(CharacterCreateTest, TheNameGoesUpToItsFirstZero)
{
    std::array<std::byte, 10> name = NameBytes("abcd");
    name[6] = std::byte{'x'};

    EXPECT_EQ(NameUpToItsEnd(name), NameBytes("abcd"));
    EXPECT_TRUE(SameBytes(Xor32Decode(WriteCharacterCreate(name, 0).packet).Bytes(),
                          Bytes({0xC1, 0x0F, 0xF3, 0x01, 'a', 'b', 'c', 'd', 0, 0, 0, 0, 0, 0, 0x00})));
}

// Un texto de hasta 10 bytes sin ceros es un nombre; uno de 11, o con un cero, no.
// El nombre como texto, para el formato del aviso 0x5e (sprintf con %s): hasta su primer cero, o
// los 10 bytes si lo llena.
TEST(CharacterCreateTest, TheNameAsTextEndsAtItsFirstZero)
{
    EXPECT_EQ(NameAsText(NameBytes("abcd")), "abcd");
    EXPECT_EQ(NameAsText(NameBytes("abcdefghij")), "abcdefghij");
    std::array<std::byte, 10> name = NameBytes("ab");
    name[5] = std::byte{'x'};
    EXPECT_EQ(NameAsText(name), "ab");
    EXPECT_EQ(NameAsText(std::array<std::byte, 10>{}), "");
}

TEST(CharacterCreateTest, ANameHasUpToTenBytes)
{
    EXPECT_EQ(NameFromText("abcdefghij"), std::optional{NameBytes("abcdefghij")});
    EXPECT_EQ(NameFromText("char0001"), std::optional{NameBytes("char0001")});
    EXPECT_EQ(NameFromText("abcdefghijk"), std::nullopt);
    EXPECT_EQ(NameFromText(std::string("ab\0cd", 5)), std::nullopt);
}

// La respuesta con 1: el nombre en pkt[5..14], el lugar en pkt[0xf], el nivel en pkt[0x10..0x11] y la
// clase en pkt[0x12].
TEST(CharacterCreateTest, ReadsTheNewCharacter)
{
    const auto read = ReadCharacterCreateResult(PacketOf(Bytes(
        {0xC1, 0x13, 0xF3, 0x01, 0x01, 'c', 'h', 'a', 'r', '0', '0', '0', '2', 0x00, 0x00, 0x03, 0x2C, 0x01, 0x40})));

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().result, CharacterCreated);
    EXPECT_EQ(read.Value().name, NameBytes("char0002"));
    EXPECT_EQ(read.Value().slot, 3);
    EXPECT_EQ(read.Value().level, 300);
    EXPECT_EQ(CharacterClassOf(read.Value().classByte), 2);
}

// Con 0 o 2, 0x4c2290 solo abre un aviso: no lee más.
TEST(CharacterCreateTest, ARefusalOnlyHasItsResult)
{
    for (const int result : {0, 2, 7})
    {
        const auto read =
            ReadCharacterCreateResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x01, static_cast<unsigned char>(result)})));

        ASSERT_TRUE(read.HasValue()) << result;
        EXPECT_EQ(read.Value().result, result);
    }
}

// 0x4c2290 no mira el largo: sin el resultado, o con 1 y sin la clase, MuRet no lo lee (TooShort).
// Otro subcódigo no es esta respuesta.
TEST(CharacterCreateTest, AShortReplyIsTooShort)
{
    EXPECT_EQ(ErrorOf(ReadCharacterCreateResult(PacketOf(Bytes({0xC1, 0x04, 0xF3, 0x01})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadCharacterCreateResult(PacketOf(Bytes({0xC1, 0x12, 0xF3, 0x01, 0x01, 'c', 'h', 'a', 'r', '0',
                                                                '0', '0', '2', 0x00, 0x00, 0x03, 0x2C, 0x01})))),
              ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadCharacterCreateResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x02, 0x01})))),
              ProtocolError::WrongPacket);
}
} // namespace
