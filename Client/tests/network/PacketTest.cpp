// Client::Network::Packet: un paquete siempre válido, armado con FromBytes.

#include "Network/Packet.h"

#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace
{

using Client::Network::DeclaredSize;
using Client::Network::HeaderSize;
using Client::Network::MaxPacketSize;
using Client::Network::MinPacketSize;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketType;
using Client::Network::PacketTypeFromByte;
using Client::Tests::SameBytes;

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

// Un C2 de `size` bytes, con el tamaño en big-endian y el resto en `fill`.
std::vector<std::byte> C2OfSize(std::size_t size, std::byte fill)
{
    std::vector<std::byte> bytes(size, fill);
    bytes[0] = std::byte{0xC2};
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
    return bytes;
}

PacketError ErrorOf(std::vector<std::byte> bytes)
{
    const auto packet = Packet::FromBytes(std::move(bytes));
    EXPECT_FALSE(packet.HasValue());
    return packet.HasValue() ? PacketError{} : packet.Error();
}

TEST(PacketTest, KnowsTheFourTypesAndTheirHeaders)
{
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC1}), PacketType::C1);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC4}), PacketType::C4);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0xC5}), std::nullopt);
    EXPECT_EQ(PacketTypeFromByte(std::byte{0x00}), std::nullopt);

    EXPECT_EQ(HeaderSize(PacketType::C1), 2u);
    EXPECT_EQ(HeaderSize(PacketType::C3), 2u);
    EXPECT_EQ(HeaderSize(PacketType::C2), 3u);
    EXPECT_EQ(HeaderSize(PacketType::C4), 3u);
    EXPECT_EQ(MinPacketSize(PacketType::C1), 3u);
    EXPECT_EQ(MinPacketSize(PacketType::C4), 4u);
}

// El tamaño de un C2/C4 es big-endian, y hace falta la cabecera entera para leerlo.
TEST(PacketTest, ReadsTheDeclaredSize)
{
    EXPECT_EQ(DeclaredSize(PacketType::C1, Bytes({0xC1, 0x04})), 4u);
    EXPECT_EQ(DeclaredSize(PacketType::C2, Bytes({0xC2, 0x01, 0x2C})), 300u);
    EXPECT_EQ(DeclaredSize(PacketType::C2, Bytes({0xC2, 0x01})), std::nullopt);
    EXPECT_EQ(DeclaredSize(PacketType::C3, Bytes({0xC3})), std::nullopt);
}

TEST(PacketTest, AcceptsAC1AndAC2)
{
    const auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(c1.HasValue());
    EXPECT_EQ(c1.Value().Type(), PacketType::C1);
    EXPECT_TRUE(SameBytes(c1.Value().Bytes(), Bytes({0xC1, 0x04, 0x20, 0xAA})));

    const auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c2.HasValue());
    EXPECT_EQ(c2.Value().Type(), PacketType::C2);
}

TEST(PacketTest, RejectsADeclaredSizeThatIsNotItsSize)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x05, 0x20, 0xAA})), PacketError::SizeMismatch);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x03, 0x20, 0xAA})), PacketError::SizeMismatch);
}

TEST(PacketTest, RejectsAnInvalidType)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC5, 0x04, 0x20, 0xAA})), PacketError::InvalidType);
}

// Nada, una cabecera incompleta, y un tamaño que no llega al opcode. Con C1 02 no se lee un opcode
// que no está.
TEST(PacketTest, RejectsWhatIsSmallerThanHeaderAndOpcode)
{
    EXPECT_EQ(ErrorOf({}), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC1})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x00})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x02})), PacketError::SizeTooSmall);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x00, 0x03})), PacketError::SizeTooSmall);
}

// El más grande que entra mide MaxPacketSize; uno más es TooLarge, aunque su tamaño sea el que mide.
TEST(PacketTest, RejectsMoreThanMaxPacketSize)
{
    EXPECT_TRUE(Packet::FromBytes(C2OfSize(MaxPacketSize, std::byte{0x11})).HasValue());
    EXPECT_EQ(ErrorOf(C2OfSize(MaxPacketSize + 1, std::byte{0x11})), PacketError::TooLarge);
}

// El opcode está en claro en C1 y C2, después de la cabecera; en C3 y C4 viaja cifrado.
TEST(PacketTest, OpcodeOnlyInC1AndC2)
{
    EXPECT_EQ(Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA})).Value().Opcode(), std::uint8_t{0x20});
    EXPECT_EQ(Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})).Value().Opcode(), std::uint8_t{0x52});
    EXPECT_EQ(Packet::FromBytes(Bytes({0xC3, 0x04, 0x20, 0xAA})).Value().Opcode(), std::nullopt);
    EXPECT_EQ(Packet::FromBytes(Bytes({0xC4, 0x00, 0x05, 0x52, 0xAA})).Value().Opcode(), std::nullopt);
}

// El cuerpo empieza después de la cabecera, en el opcode, y se puede cambiar en el lugar.
TEST(PacketTest, MutableBodyStartsAfterTheHeader)
{
    auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA})).Value();
    ASSERT_EQ(c1.MutableBody().size(), 2u);
    EXPECT_EQ(c1.MutableBody()[0], std::byte{0x20});
    c1.MutableBody()[1] = std::byte{0x55};
    EXPECT_TRUE(SameBytes(c1.Bytes(), Bytes({0xC1, 0x04, 0x20, 0x55})));

    auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})).Value();
    ASSERT_EQ(c2.MutableBody().size(), 2u);
    EXPECT_EQ(c2.MutableBody()[0], std::byte{0x52});
}

TEST(PacketTest, TakeBytesGivesTheSameBytesBack)
{
    auto packet = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})).Value();

    const std::vector<std::byte> bytes = std::move(packet).TakeBytes();

    EXPECT_TRUE(SameBytes(bytes, Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})));
    EXPECT_TRUE(Packet::FromBytes(bytes).HasValue());
}

} // namespace
