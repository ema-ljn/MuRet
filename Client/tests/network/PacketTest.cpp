#include "Network/Packet.h"

#include "support/Bytes.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

// Client::Network::Packet: un paquete válido, armado con FromBytes.
namespace
{
using Client::Network::AppendHeader;
using Client::Network::CheckDeclaredSize;
using Client::Network::DeclaredSize;
using Client::Network::HeaderSize;
using Client::Network::MaxPacketSize;
using Client::Network::MinPacketSize;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketType;
using Client::Network::PacketTypeFromByte;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

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

// AppendHeader escribe la cabecera que lee DeclaredSize, detrás de lo que ya había: el tamaño en un
// byte en C1 y C3, y en dos, big-endian, en C2 y C4.
TEST(PacketTest, AppendsTheHeaderAfterWhatWasThere)
{
    std::vector<std::byte> out = Bytes({0x99});

    AppendHeader(out, PacketType::C1, 4);
    AppendHeader(out, PacketType::C2, 300);
    AppendHeader(out, PacketType::C3, 0xFF);
    AppendHeader(out, PacketType::C4, MaxPacketSize);

    EXPECT_TRUE(SameBytes(out, Bytes({0x99, 0xC1, 0x04, 0xC2, 0x01, 0x2C, 0xC3, 0xFF, 0xC4, 0x20, 0x00})));
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

// El orden de los errores: un tamaño que no sirve se dice antes que un tamaño que no es el que
// mide. C2 20 01 y C2 FF FF son solo la cabecera, y C1 02 20 AA mide 4.
TEST(PacketTest, SaysABadDeclaredSizeBeforeAMismatch)
{
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0x20, 0x01})), PacketError::TooLarge);
    EXPECT_EQ(ErrorOf(Bytes({0xC2, 0xFF, 0xFF})), PacketError::TooLarge);
    EXPECT_EQ(ErrorOf(Bytes({0xC1, 0x02, 0x20, 0xAA})), PacketError::SizeTooSmall);
}

// La regla del tamaño en los bordes de cada tipo: MinPacketSize y MaxPacketSize entran, uno menos y
// uno más no.
TEST(PacketTest, ChecksTheDeclaredSizeAtTheEdges)
{
    for (const PacketType type : {PacketType::C1, PacketType::C2, PacketType::C3, PacketType::C4})
    {
        EXPECT_EQ(CheckDeclaredSize(type, 0), PacketError::SizeTooSmall);
        EXPECT_EQ(CheckDeclaredSize(type, MinPacketSize(type) - 1), PacketError::SizeTooSmall);
        EXPECT_EQ(CheckDeclaredSize(type, MinPacketSize(type)), std::nullopt);
        EXPECT_EQ(CheckDeclaredSize(type, MaxPacketSize), std::nullopt);
        EXPECT_EQ(CheckDeclaredSize(type, MaxPacketSize + 1), PacketError::TooLarge);
    }
}

// El opcode está en claro en C1 y C2, después de la cabecera; en C3 y C4 viaja cifrado.
TEST(PacketTest, OpcodeOnlyInC1AndC2)
{
    const auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    const auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    const auto c3 = Packet::FromBytes(Bytes({0xC3, 0x04, 0x20, 0xAA}));
    const auto c4 = Packet::FromBytes(Bytes({0xC4, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c1.HasValue() && c2.HasValue() && c3.HasValue() && c4.HasValue());

    EXPECT_EQ(c1.Value().Opcode(), std::uint8_t{0x20});
    EXPECT_EQ(c2.Value().Opcode(), std::uint8_t{0x52});
    EXPECT_EQ(c3.Value().Opcode(), std::nullopt);
    EXPECT_EQ(c4.Value().Opcode(), std::nullopt);
}

// El cuerpo empieza después de la cabecera, en el opcode, y se puede cambiar en el lugar.
TEST(PacketTest, MutableBodyStartsAfterTheHeader)
{
    auto c1 = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(c1.HasValue());
    ASSERT_EQ(c1.Value().MutableBody().size(), 2u);
    EXPECT_EQ(c1.Value().MutableBody()[0], std::byte{0x20});
    c1.Value().MutableBody()[1] = std::byte{0x55};
    EXPECT_TRUE(SameBytes(c1.Value().Bytes(), Bytes({0xC1, 0x04, 0x20, 0x55})));

    auto c2 = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(c2.HasValue());
    ASSERT_EQ(c2.Value().MutableBody().size(), 2u);
    EXPECT_EQ(c2.Value().MutableBody()[0], std::byte{0x52});
}

TEST(PacketTest, TakeBytesGivesTheSameBytesBack)
{
    auto loaded = Packet::FromBytes(Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA}));
    ASSERT_TRUE(loaded.HasValue());
    Packet packet = std::move(loaded).Value();

    const std::vector<std::byte> bytes = std::move(packet).TakeBytes();

    EXPECT_TRUE(SameBytes(bytes, Bytes({0xC2, 0x00, 0x05, 0x52, 0xAA})));
    EXPECT_TRUE(Packet::FromBytes(bytes).HasValue());
}

// Lo que queda después de TakeBytes está vacío, y no se lee nada fuera de él.
TEST(PacketTest, TakeBytesLeavesItEmpty)
{
    auto loaded = Packet::FromBytes(Bytes({0xC1, 0x04, 0x20, 0xAA}));
    ASSERT_TRUE(loaded.HasValue());
    Packet packet = std::move(loaded).Value();

    const std::vector<std::byte> bytes = std::move(packet).TakeBytes();

    EXPECT_EQ(bytes.size(), 4u);
    EXPECT_TRUE(packet.Bytes().empty());
    EXPECT_TRUE(packet.MutableBody().empty());
    EXPECT_EQ(packet.Opcode(), std::nullopt);
}
} // namespace
