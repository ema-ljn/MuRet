// Client::Network::PacketReader: la tabla de vectores del plan de la CL2 (CL2.1, paso 4), con los
// casos de red_reference.py --selftest que no son del cifrado y los quirks que corrige.

#include "Network/PacketReader.h"

#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace
{

using Client::Network::MaxPacketSize;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketType;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

ByteVector Bytes(std::initializer_list<int> values)
{
    ByteVector bytes;
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

ByteVector Join(const ByteVector& first, const ByteVector& second)
{
    ByteVector joined = first;
    joined.insert(joined.end(), second.begin(), second.end());
    return joined;
}

// Lo que sacó el lector hasta quedarse sin paquetes, o el error con que se paró.
struct Drained
{
    std::vector<ByteVector> packets;
    std::optional<PacketError> error;
};

Drained Drain(PacketReader& reader)
{
    Drained drained;
    while (true)
    {
        auto next = reader.Next();
        if (!next.HasValue())
        {
            drained.error = next.Error();
            return drained;
        }
        if (!next.Value().has_value())
        {
            return drained;
        }
        const std::span<const std::byte> bytes = next.Value()->Bytes();
        drained.packets.emplace_back(bytes.begin(), bytes.end());
    }
}

// Los dos paquetes del caso 1 de red_reference.py.
const ByteVector FirstC1 = Bytes({0xC1, 0x05, 0x10, 0x01, 0x02});
const ByteVector SecondC2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});

// Un paquete de `size` bytes del tipo `type`, con el tamaño en su cabecera y el resto en `fill`.
ByteVector PacketOfSize(PacketType type, std::size_t size, std::byte fill)
{
    ByteVector bytes(size, fill);
    bytes[0] = static_cast<std::byte>(type);
    if (type == PacketType::C1 || type == PacketType::C3)
    {
        bytes[1] = static_cast<std::byte>(size);
        return bytes;
    }
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
    return bytes;
}

TEST(PacketReaderTest, CutsAC1)
{
    PacketReader reader;
    reader.Feed(Bytes({0xC1, 0x04, 0x20, 0xAA}));

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], Bytes({0xC1, 0x04, 0x20, 0xAA})));
    EXPECT_FALSE(drained.error.has_value());
    EXPECT_EQ(reader.Pending(), 0u);
}

// C2 01 2C son 300 bytes: el tamaño big-endian, con el byte alto distinto de 0.
TEST(PacketReaderTest, CutsAC2Of300Bytes)
{
    const ByteVector packet = PacketOfSize(PacketType::C2, 300, std::byte{0x5A});
    PacketReader reader;
    reader.Feed(packet);

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], packet));
}

// Un C3 y un C4 se cortan como un C1 y un C2, sin descifrarlos.
TEST(PacketReaderTest, CutsC3AndC4WithoutDecrypting)
{
    const ByteVector c3 = PacketOfSize(PacketType::C3, 13, std::byte{0x77});
    const ByteVector c4 = PacketOfSize(PacketType::C4, 300, std::byte{0x88});
    PacketReader reader;
    reader.Feed(Join(c3, c4));

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 2u);
    EXPECT_TRUE(SameBytes(drained.packets[0], c3));
    EXPECT_TRUE(SameBytes(drained.packets[1], c4));
}

// El caso 1 de red_reference.py: el C1 y los 3 primeros bytes del C2 en una lectura, y el resto en
// otra.
TEST(PacketReaderTest, KeepsThePartialRestForTheNextFeed)
{
    PacketReader reader;
    const ByteVector stream = Join(FirstC1, SecondC2);
    const std::span<const std::byte> bytes(stream);
    const std::size_t firstRead = FirstC1.size() + 3;

    reader.Feed(bytes.first(firstRead));
    const Drained first = Drain(reader);
    ASSERT_EQ(first.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(first.packets[0], FirstC1));
    EXPECT_EQ(reader.Pending(), 3u);

    reader.Feed(bytes.subspan(firstRead));
    const Drained second = Drain(reader);
    ASSERT_EQ(second.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(second.packets[0], SecondC2));
    EXPECT_EQ(reader.Pending(), 0u);
}

// Q-C03: partido después de C2 o de C2 00, el original arma el tamaño con bytes viejos (con C2 00
// saca C1 05 06 20 03, el jefe de la CL2.0). El lector espera los que faltan y da los mismos dos
// paquetes.
TEST(PacketReaderTest, WaitsOnAnIncompleteC2Header)
{
    for (const std::size_t headerBytes : {std::size_t{1}, std::size_t{2}})
    {
        PacketReader reader;
        const ByteVector stream = Join(FirstC1, SecondC2);
        const std::span<const std::byte> bytes(stream);
        const std::size_t firstRead = FirstC1.size() + headerBytes;

        reader.Feed(bytes.first(firstRead));
        const Drained first = Drain(reader);
        reader.Feed(bytes.subspan(firstRead));
        const Drained second = Drain(reader);

        ASSERT_EQ(first.packets.size(), 1u) << "partido después de " << headerBytes << " bytes del C2";
        EXPECT_TRUE(SameBytes(first.packets[0], FirstC1));
        ASSERT_EQ(second.packets.size(), 1u) << "partido después de " << headerBytes << " bytes del C2";
        EXPECT_TRUE(SameBytes(second.packets[0], SecondC2));
    }
}

// Q-C03 con el primer byte de un C1: el original leería el tamaño de un byte viejo.
TEST(PacketReaderTest, WaitsOnALoneC1Type)
{
    PacketReader reader;

    reader.Feed(Bytes({0xC1}));
    EXPECT_TRUE(Drain(reader).packets.empty());
    reader.Feed(Bytes({0x04, 0x20, 0xAA}));
    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], Bytes({0xC1, 0x04, 0x20, 0xAA})));
}

// El caso 2 de red_reference.py: con la cabecera sola espera el resto.
TEST(PacketReaderTest, WaitsForTheBody)
{
    PacketReader reader;

    reader.Feed(Bytes({0xC1, 0x03}));
    const Drained first = Drain(reader);
    EXPECT_TRUE(first.packets.empty());
    EXPECT_FALSE(first.error.has_value());

    reader.Feed(Bytes({0x00}));
    const Drained second = Drain(reader);
    ASSERT_EQ(second.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(second.packets[0], Bytes({0xC1, 0x03, 0x00})));
}

// En el cable hubo lecturas con 2 a 7 paquetes juntos (03, "Cadena de recepción").
TEST(PacketReaderTest, CutsSeveralPacketsInOneFeed)
{
    ByteVector stream;
    for (int k = 0; k < 7; ++k)
    {
        stream = Join(stream, Bytes({0xC1, 0x04, 0x20, k}));
    }
    PacketReader reader;
    reader.Feed(stream);

    const Drained drained = Drain(reader);

    ASSERT_EQ(drained.packets.size(), 7u);
    EXPECT_TRUE(SameBytes(drained.packets[6], Bytes({0xC1, 0x04, 0x20, 6})));
}

// El caso 3 de red_reference.py y Q-C38: un tipo inválido es un error, y el paquete bueno de atrás
// no sale (el original descarta los dos y sigue con lo que llegue).
TEST(PacketReaderTest, StopsOnAnInvalidType)
{
    PacketReader reader;
    reader.Feed(Join(Bytes({0x00, 0x05, 0x01, 0x02, 0x03}), FirstC1));

    const Drained drained = Drain(reader);

    EXPECT_TRUE(drained.packets.empty());
    EXPECT_EQ(drained.error, PacketError::InvalidType);
}

// El caso 4 de red_reference.py y Q-C39: el tamaño 0 y los que no llegan al opcode son un error
// apenas llega la cabecera. El original, con 0, no avanza más; con 1 o 2 saca un paquete sin
// opcode.
TEST(PacketReaderTest, StopsOnASizeSmallerThanHeaderAndOpcode)
{
    for (const ByteVector& header : {Bytes({0xC1, 0x00}), Bytes({0xC1, 0x01}), Bytes({0xC1, 0x02}),
                                     Bytes({0xC2, 0x00, 0x00}), Bytes({0xC2, 0x00, 0x03})})
    {
        PacketReader reader;
        reader.Feed(header);

        const Drained drained = Drain(reader);

        EXPECT_TRUE(drained.packets.empty());
        EXPECT_EQ(drained.error, PacketError::SizeTooSmall) << "cabecera de " << header.size() << " bytes";
    }
}

// Q-C05: C2 20 00 son MaxPacketSize bytes y se esperan; C2 20 01 se rechaza con la cabecera sola.
TEST(PacketReaderTest, RejectsMoreThanMaxPacketSizeWithTheHeaderAlone)
{
    PacketReader largest;
    largest.Feed(Bytes({0xC2, 0x20, 0x00}));
    const Drained waiting = Drain(largest);
    EXPECT_TRUE(waiting.packets.empty());
    EXPECT_FALSE(waiting.error.has_value());

    const ByteVector packet = PacketOfSize(PacketType::C2, MaxPacketSize, std::byte{0x33});
    largest.Feed(std::span<const std::byte>(packet).subspan(3));
    ASSERT_EQ(Drain(largest).packets.size(), 1u);

    PacketReader tooLarge;
    tooLarge.Feed(Bytes({0xC2, 0x20, 0x01}));
    EXPECT_EQ(Drain(tooLarge).error, PacketError::TooLarge);
}

// Después de un error, Next() lo repite aunque llegue un paquete bueno, y no guarda lo que llega.
TEST(PacketReaderTest, RepeatsTheErrorAfterwards)
{
    PacketReader reader;
    reader.Feed(Bytes({0xC5}));
    ASSERT_EQ(Drain(reader).error, PacketError::InvalidType);

    reader.Feed(FirstC1);
    const Drained drained = Drain(reader);

    EXPECT_TRUE(drained.packets.empty());
    EXPECT_EQ(drained.error, PacketError::InvalidType);
}

// Sin bytes no hay paquete ni error.
TEST(PacketReaderTest, NothingFedGivesNothing)
{
    PacketReader reader;
    reader.Feed({});

    const Drained drained = Drain(reader);

    EXPECT_TRUE(drained.packets.empty());
    EXPECT_FALSE(drained.error.has_value());
}

// Un número de 0 a n - 1 tomado de la salida del motor (engine() % n): el estándar fija los números
// de std::mt19937, pero no los de std::uniform_int_distribution, que cambian entre bibliotecas. La
// salida de std::mt19937 tiene 32 bits, así que entra en un std::uint32_t.
std::uint32_t Below(std::mt19937& engine, std::uint32_t n)
{
    return static_cast<std::uint32_t>(engine() % n);
}

// Un paquete bien formado al azar: tipo C1 a C4 y tamaño desde el mínimo. Los C2/C4 llegan a 600
// para que el byte alto del tamaño no sea siempre 0.
ByteVector RandomPacket(std::mt19937& engine)
{
    // Los tipos posibles, y el tamaño más grande que se genera para cada clase de cabecera.
    constexpr PacketType Types[] = {PacketType::C1, PacketType::C2, PacketType::C3, PacketType::C4};
    constexpr std::uint32_t MaxOneByteSize = 0xFF;
    constexpr std::uint32_t MaxTwoByteSize = 600;

    const PacketType type = Types[Below(engine, static_cast<std::uint32_t>(std::size(Types)))];
    const bool oneByteSize = type == PacketType::C1 || type == PacketType::C3;
    const std::uint32_t minSize = oneByteSize ? 3 : 4;
    const std::uint32_t maxSize = oneByteSize ? MaxOneByteSize : MaxTwoByteSize;
    ByteVector packet = PacketOfSize(type, minSize + Below(engine, maxSize - minSize + 1), std::byte{0});
    const std::size_t headerSize = oneByteSize ? 2 : 3;
    for (std::size_t i = headerSize; i < packet.size(); ++i)
    {
        packet[i] = static_cast<std::byte>(Below(engine, 0x100));
    }
    return packet;
}

// Propiedad: el mismo flujo partido al azar en lecturas da los mismos paquetes. Acá sí importa:
// cada Feed trae una lectura entera, y un corte puede caer en cualquier byte de una cabecera.
TEST(PacketReaderTest, RandomSplitsGiveTheSamePackets)
{
    // Semilla fija, cuántos paquetes lleva el flujo, y la lectura más larga en que se parte.
    constexpr std::mt19937::result_type Seed = 20261102;
    constexpr std::uint32_t PacketCount = 300;
    constexpr std::uint32_t MaxReadSize = 64;

    std::mt19937 engine(Seed);
    std::vector<ByteVector> expected;
    ByteVector stream;
    for (std::uint32_t k = 0; k < PacketCount; ++k)
    {
        expected.push_back(RandomPacket(engine));
        stream = Join(stream, expected.back());
    }

    PacketReader reader;
    std::vector<ByteVector> got;
    const std::span<const std::byte> bytes(stream);
    for (std::size_t offset = 0; offset < bytes.size();)
    {
        const std::size_t read = std::min<std::size_t>(1 + Below(engine, MaxReadSize), bytes.size() - offset);
        reader.Feed(bytes.subspan(offset, read));
        Drained drained = Drain(reader);
        ASSERT_FALSE(drained.error.has_value());
        for (ByteVector& packet : drained.packets)
        {
            got.push_back(std::move(packet));
        }
        offset += read;
    }

    EXPECT_EQ(got, expected);
    EXPECT_EQ(reader.Pending(), 0u);
}

} // namespace
