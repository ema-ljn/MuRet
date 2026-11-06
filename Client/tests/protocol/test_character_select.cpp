#include "Protocol/Character/CharacterSelect.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// Client::Protocol::Character, entrar al mundo con el elegido (F3 03, C->S, 0x636290), con nombres
// sintéticos.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Outgoing;
using Client::Protocol::ProtocolError;
using Client::Protocol::Character::CharacterSelectResult;
using Client::Protocol::Character::ReadCharacterSelectResult;
using Client::Protocol::Character::WriteCharacterSelect;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::NameBytes;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// 04-protocolo, "La entrada al mundo": fixture sintético completo, con valores asimétricos
// escritos a mano. Q-C144: 0x32..0x33 sí se leen como Leadership, a diferencia de 0x34..0x37.
std::vector<std::byte> SelectResultBytes()
{
    return Bytes({0xC1, 0x34, 0xF3, 0x03, 0x12, 0x34, 0x56, 0xFF, 0x24, 0x68, 0xAC, 0xE0, 0x35,
                  0x79, 0xBD, 0xF1, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x13, 0x57, 0x9B, 0xDF, 0x24,
                  0x68, 0xAC, 0xE0, 0x35, 0x79, 0xBD, 0xF1, 0x46, 0x8A, 0xCE, 0x02, 0x57, 0xA5,
                  0x5A, 0x13, 0x57, 0x9B, 0xDF, 0x81, 0xFE, 0x24, 0x68, 0xAC, 0xE0, 0x5B, 0xA7});
}

void ExpectSelectResultFields(const CharacterSelectResult& result)
{
    EXPECT_EQ(result.x, 0x12);
    EXPECT_EQ(result.y, 0x34);
    EXPECT_EQ(result.map, 0x56);
    EXPECT_EQ(result.direction, 0xFF);
    EXPECT_EQ(result.experience, 0xE0AC6824u);
    EXPECT_EQ(result.nextExperience, 0xF1BD7935u);
    EXPECT_EQ(result.levelUpPoints, 0x9A78);
    EXPECT_EQ(result.strength, 0xDEBC);
    EXPECT_EQ(result.agility, 0x13F0);
    EXPECT_EQ(result.vitality, 0x9B57);
    EXPECT_EQ(result.energy, 0x24DF);
    EXPECT_EQ(result.life, 0xAC68);
    EXPECT_EQ(result.maxLife, 0x35E0);
    EXPECT_EQ(result.mana, 0xBD79);
    EXPECT_EQ(result.maxMana, 0x46F1);
    EXPECT_EQ(result.bp, 0xCE8A);
    EXPECT_EQ(result.maxBp, 0x5702);
    EXPECT_EQ(result.zen, 0xDF9B5713u);
    EXPECT_EQ(result.pk, 0x81);
    EXPECT_EQ(result.controlCode, 0xFE);
    EXPECT_EQ(result.addedPoints, 0x6824);
    EXPECT_EQ(result.maxAddedPoints, 0xE0AC);
    EXPECT_EQ(result.leadership, 0xA75B);
}

// C1 0E F3 03 y los 10 bytes del nombre como vienen en la lista, con el XOR32 desde el byte 3 y en
// claro (0x6362b2..0x63638d).
TEST(CharacterSelectTest, TheRequestIsC10EF303WithTheName)
{
    const Outgoing select = WriteCharacterSelect(NameBytes("char0001"));

    EXPECT_EQ(select.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(select.packet).Bytes(),
                          Bytes({0xC1, 0x0E, 0xF3, 0x03, 'c', 'h', 'a', 'r', '0', '0', '0', '1', 0x00, 0x00})));
    EXPECT_FALSE(SameBytes(select.packet.Bytes(), Xor32Decode(select.packet).Bytes())) << "salió sin el XOR32";
}

// Un nombre de 10 bytes sale entero, sin terminador.
TEST(CharacterSelectTest, AFullNameGoesWithoutATerminator)
{
    const Outgoing select = WriteCharacterSelect(NameBytes("abcdefghij"));

    EXPECT_TRUE(SameBytes(Xor32Decode(select.packet).Bytes(),
                          Bytes({0xC1, 0x0E, 0xF3, 0x03, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'})));
}
TEST(CharacterSelectTest, ReadsEverySelectResultFieldLittleEndian)
{
    const auto read = ReadCharacterSelectResult(PacketOf(SelectResultBytes()));

    ASSERT_TRUE(read.HasValue());
    ExpectSelectResultFields(read.Value());
}

// Cable histórico, conexión 2 a las 13:21:30: solo F3/03 S->C, permitido por wirelib.is_safe.
// Llegó como C3; estos son sus 52 bytes C1 rearmados por el decoder existente, sin nombres.
TEST(CharacterSelectTest, ReadsTheNativeSelectResult)
{
    const auto read = ReadCharacterSelectResult(PacketOf(Bytes(
        {0xC1, 0x34, 0xF3, 0x03, 0x85, 0x7F, 0x00, 0x00, 0x7E, 0xB9, 0xA1, 0x00, 0x20, 0x52, 0xA6, 0x00, 0xD6, 0x4D,
         0x34, 0x00, 0x2D, 0x00, 0x25, 0x00, 0x19, 0x00, 0x5E, 0x01, 0x5E, 0x01, 0x54, 0x00, 0x54, 0x00, 0x1A, 0x00,
         0x34, 0x00, 0x00, 0x00, 0xD8, 0x03, 0x00, 0x00, 0x02, 0x00, 0x33, 0x4C, 0x16, 0x00, 0xFF, 0xFF})));

    ASSERT_TRUE(read.HasValue());
    const CharacterSelectResult& result = read.Value();
    EXPECT_EQ(result.x, 133);
    EXPECT_EQ(result.y, 127);
    EXPECT_EQ(result.map, 0);
    EXPECT_EQ(result.direction, 0);
    EXPECT_EQ(result.experience, 0x00A1B97Eu);
    EXPECT_EQ(result.nextExperience, 0x00A65220u);
    EXPECT_EQ(result.levelUpPoints, 0x4DD6);
    EXPECT_EQ(result.strength, 52);
    EXPECT_EQ(result.agility, 45);
    EXPECT_EQ(result.vitality, 37);
    EXPECT_EQ(result.energy, 25);
    EXPECT_EQ(result.life, 350);
    EXPECT_EQ(result.maxLife, 350);
    EXPECT_EQ(result.mana, 84);
    EXPECT_EQ(result.maxMana, 84);
    EXPECT_EQ(result.bp, 26);
    EXPECT_EQ(result.maxBp, 52);
    EXPECT_EQ(result.zen, 984);
    EXPECT_EQ(result.pk, 2);
    EXPECT_EQ(result.controlCode, 0);
    EXPECT_EQ(result.addedPoints, 0x4C33);
    EXPECT_EQ(result.maxAddedPoints, 22);
    EXPECT_EQ(result.leadership, 0xFFFF);
}

TEST(CharacterSelectTest, ReadsLeadershipFromTheTwoInPacketPaddingBytes)
{
    auto bytes = SelectResultBytes();
    const auto asymmetric = ReadCharacterSelectResult(PacketOf(bytes));
    ASSERT_TRUE(asymmetric.HasValue());
    EXPECT_EQ(asymmetric.Value().leadership, 0xA75B);

    bytes[0x32] = std::byte{0xFF};
    bytes[0x33] = std::byte{0xFF};
    const auto maximum = ReadCharacterSelectResult(PacketOf(bytes));
    ASSERT_TRUE(maximum.HasValue());
    EXPECT_EQ(maximum.Value().leadership, 0xFFFF);
}

TEST(CharacterSelectTest, IgnoresTheUnusedPaddingBeforeZen)
{
    auto bytes = SelectResultBytes();
    bytes[0x26] = std::byte{0x17};
    bytes[0x27] = std::byte{0xE9};
    const auto read = ReadCharacterSelectResult(PacketOf(bytes));

    ASSERT_TRUE(read.HasValue());
    ExpectSelectResultFields(read.Value());
}

TEST(CharacterSelectTest, EveryTruncatedSelectResultIsTooShort)
{
    for (std::size_t size = 3; size < 0x34; ++size)
    {
        SCOPED_TRACE(size);
        auto bytes = SelectResultBytes();
        bytes.resize(size);
        bytes[1] = static_cast<std::byte>(size);

        EXPECT_EQ(ErrorOf(ReadCharacterSelectResult(PacketOf(bytes))), ProtocolError::TooShort);
    }
}

TEST(CharacterSelectTest, SelectResultRejectsWrongTypesOpcodesAndSubcodes)
{
    for (const auto& bytes :
         {Bytes({0xC2, 0x00, 0x05, 0xF3, 0x03}), Bytes({0xC3, 0x04, 0xF3, 0x03}), Bytes({0xC4, 0x00, 0x05, 0xF3, 0x03}),
          Bytes({0xC1, 0x04, 0xF2, 0x03}), Bytes({0xC1, 0x04, 0xF3, 0x04})})
    {
        EXPECT_EQ(ErrorOf(ReadCharacterSelectResult(PacketOf(bytes))), ProtocolError::WrongPacket);
    }
    EXPECT_EQ(ErrorOf(ReadCharacterSelectResult(PacketOf(Bytes({0xC1, 0x03, 0xF3})))), ProtocolError::TooShort);
}

TEST(CharacterSelectTest, SelectResultIgnoresTrailingBytes)
{
    auto bytes = SelectResultBytes();
    // Q-C144: estos bytes no pertenecen al paquete GS; el lector no los expone como campos.
    bytes.insert(bytes.end(), {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}});
    bytes[1] = static_cast<std::byte>(bytes.size());
    const auto read = ReadCharacterSelectResult(PacketOf(bytes));

    ASSERT_TRUE(read.HasValue());
    ExpectSelectResultFields(read.Value());
}

TEST(CharacterSelectTest, ReadsZeroAndMaximumSelectResultFields)
{
    for (const int value : {0x00, 0xFF})
    {
        SCOPED_TRACE(value);
        auto bytes = SelectResultBytes();
        std::fill(bytes.begin() + 4, bytes.end(), static_cast<std::byte>(value));
        const auto read = ReadCharacterSelectResult(PacketOf(bytes));
        ASSERT_TRUE(read.HasValue());
        const CharacterSelectResult& result = read.Value();
        const std::uint16_t word = value == 0 ? 0 : 0xFFFF;
        const std::uint32_t dword = value == 0 ? 0 : 0xFFFFFFFF;

        EXPECT_EQ(result.x, value);
        EXPECT_EQ(result.y, value);
        EXPECT_EQ(result.map, value);
        EXPECT_EQ(result.direction, value);
        EXPECT_EQ(result.experience, dword);
        EXPECT_EQ(result.nextExperience, dword);
        EXPECT_EQ(result.levelUpPoints, word);
        EXPECT_EQ(result.strength, word);
        EXPECT_EQ(result.agility, word);
        EXPECT_EQ(result.vitality, word);
        EXPECT_EQ(result.energy, word);
        EXPECT_EQ(result.life, word);
        EXPECT_EQ(result.maxLife, word);
        EXPECT_EQ(result.mana, word);
        EXPECT_EQ(result.maxMana, word);
        EXPECT_EQ(result.bp, word);
        EXPECT_EQ(result.maxBp, word);
        EXPECT_EQ(result.zen, dword);
        EXPECT_EQ(result.pk, value);
        EXPECT_EQ(result.controlCode, value);
        EXPECT_EQ(result.addedPoints, word);
        EXPECT_EQ(result.maxAddedPoints, word);
        EXPECT_EQ(result.leadership, word);
    }
}
} // namespace
