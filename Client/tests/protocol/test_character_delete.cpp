#include "Protocol/Character/CharacterDelete.h"

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

#include <array>
#include <cstddef>

// Client::Protocol::Character, borrar un personaje (F3 02: 0x643740 y 0x4e2b10), con un nombre y un
// código sintéticos.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Protocol::Character::CharacterDeleted;
using Client::Protocol::Character::DeleteCodeFromText;
using Client::Protocol::Character::DeleteCodeSize;
using Client::Protocol::Character::ReadCharacterDeleteResult;
using Client::Protocol::Character::WriteCharacterDelete;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::NameBytes;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// Un código sintético de 10 bytes, con dos detrás del primer cero: salen como vienen.
std::array<std::byte, DeleteCodeSize> SyntheticCode()
{
    return {std::byte{'1'}, std::byte{'2'}, std::byte{'3'}, std::byte{'4'}, std::byte{0},
            std::byte{0},   std::byte{'z'}, std::byte{0},   std::byte{'y'}, std::byte{0}};
}

// C1 18 F3 02, el nombre del elegido con ceros hasta 10 y los 10 bytes del campo como vienen, con el
// XOR32 desde el byte 3 y en claro.
TEST(CharacterDeleteTest, TheRequestIsC118F302WithTheNameAndTheCode)
{
    const auto request = WriteCharacterDelete(NameBytes("char0001"), SyntheticCode());

    EXPECT_EQ(request.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(),
                          Bytes({0xC1, 0x18, 0xF3, 0x02, 'c', 'h', 'a',  'r',  '0', '0',  '0', '1',
                                 0x00, 0x00, '1',  '2',  '3', '4', 0x00, 0x00, 'z', 0x00, 'y', 0x00})));
    EXPECT_FALSE(SameBytes(request.packet.Bytes(), Xor32Decode(request.packet).Bytes())) << "salió sin el XOR32";
}

// El nombre, hasta su primer cero (el strlen de 0x643740); el código, no.
TEST(CharacterDeleteTest, TheNameGoesUpToItsFirstZero)
{
    std::array<std::byte, 10> name = NameBytes("abcd");
    name[7] = std::byte{'x'};

    const auto request = WriteCharacterDelete(name, SyntheticCode());

    EXPECT_TRUE(
        SameBytes(Xor32Decode(request.packet).Bytes().subspan(4, 10), Bytes({'a', 'b', 'c', 'd', 0, 0, 0, 0, 0, 0})));
}

// La respuesta es pkt[4], que 0x4e2b10 lee en línea; sin él, TooShort.
// 0x643740: el texto del campo en un buffer que se vació al abrir, así que lo que no escribió el
// jugador va en cero; uno vacío se manda igual. Más de 10 bytes no entran (el campo toma 7).
TEST(CharacterDeleteTest, TheCodeIsItsTextAndZeros)
{
    std::array<std::byte, DeleteCodeSize> expected{};
    expected[0] = std::byte{'4'};
    expected[1] = std::byte{'2'};
    EXPECT_EQ(DeleteCodeFromText("42"), expected);
    EXPECT_EQ(DeleteCodeFromText(""), (std::array<std::byte, DeleteCodeSize>{}));
    const std::array<std::byte, DeleteCodeSize> full = DeleteCodeFromText("0123456789ab");
    EXPECT_EQ(full[9], std::byte{'9'});
}

TEST(CharacterDeleteTest, TheReplyIsItsResult)
{
    for (const int result : {0, 1, 3, 9})
    {
        const auto read =
            ReadCharacterDeleteResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x02, static_cast<unsigned char>(result)})));
        ASSERT_TRUE(read.HasValue()) << result;
        EXPECT_EQ(read.Value(), result);
    }
    EXPECT_EQ(CharacterDeleted, 1);
    EXPECT_EQ(ErrorOf(ReadCharacterDeleteResult(PacketOf(Bytes({0xC1, 0x04, 0xF3, 0x02})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadCharacterDeleteResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x01, 0x01})))),
              ProtocolError::WrongPacket);
}
} // namespace
