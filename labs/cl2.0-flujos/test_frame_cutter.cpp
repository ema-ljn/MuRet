#include "FrameCutter.h"

#include "FrameBytes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <random>
#include <span>
#include <utility>
#include <vector>

// Labs::FrameCutter: los casos de red_reference.py --selftest que no son del cifrado (el corte, el
// resto parcial, la cabecera incompleta, la cabecera inválida y el tamaño 0), y uno de propiedad.
// El script está en el repo de research: client-research/analysis/mainexe/scripts/.
namespace
{
using Labs::CutError;
using Labs::Frame;
using Labs::FrameCutter;
using Labs::OneByteSizeHeader;
using Labs::OpcodeSize;
using Labs::TwoByteSizeHeader;
using Labs::Tests::Bytes;
using Labs::Tests::FeedAll;
using Labs::Tests::FirstC1;
using Labs::Tests::Join;
using Labs::Tests::SecondC2;

// Caso 1: dos paquetes seguidos en una lectura salen los dos, enteros y en orden.
TEST(FrameCutterTest, CutsTwoPacketsInOneRead)
{
    FrameCutter cutter;

    const std::vector<Frame> frames = FeedAll(cutter, Join(FirstC1, SecondC2));

    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0], FirstC1);
    EXPECT_EQ(frames[1], SecondC2);
    EXPECT_EQ(cutter.Pending(), 0u);
    EXPECT_FALSE(cutter.Error().has_value());
}

// Caso 1, partido: la primera lectura trae el C1 y la cabecera del C2 (3 bytes). El C1 sale, los 3
// bytes quedan esperando, y el C2 sale entero con la segunda lectura.
TEST(FrameCutterTest, KeepsThePartialRestForTheNextRead)
{
    FrameCutter cutter;
    const Frame stream = Join(FirstC1, SecondC2);
    const std::span<const std::byte> bytes(stream);
    const std::size_t firstRead = FirstC1.size() + 3;

    const std::vector<Frame> first = FeedAll(cutter, bytes.first(firstRead));
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0], FirstC1);
    EXPECT_EQ(cutter.Pending(), 3u);

    const std::vector<Frame> second = FeedAll(cutter, bytes.subspan(firstRead));
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0], SecondC2);
    EXPECT_EQ(cutter.Pending(), 0u);
}

// Caso 2: con la cabecera entera y sin el opcode no sale nada, y tampoco es un error: espera. (En
// el original el caso 2 es otro umbral: con menos de 3 bytes en el buffer, 0x4e88a0 ni corta, en
// 0x4e8927.)
TEST(FrameCutterTest, WaitsForTheOpcode)
{
    FrameCutter cutter;

    EXPECT_TRUE(FeedAll(cutter, Bytes({0xC1, 0x03})).empty());
    EXPECT_FALSE(cutter.Error().has_value());
    EXPECT_EQ(cutter.Pending(), 2u);

    const std::vector<Frame> frames = FeedAll(cutter, Bytes({0x00}));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], Bytes({0xC1, 0x03, 0x00}));
}

// Q-C03 con un C1: con solo el byte del tipo, el original arma el tamaño con un byte viejo. El
// corte espera el tamaño, y no lo inventa.
TEST(FrameCutterTest, WaitsForTheSizeOfAC1)
{
    FrameCutter cutter;

    EXPECT_TRUE(FeedAll(cutter, Bytes({0xC1})).empty());
    EXPECT_FALSE(cutter.Error().has_value());
    EXPECT_EQ(cutter.Pending(), 1u);

    const std::vector<Frame> frames = FeedAll(cutter, Bytes({0x03, 0xF4}));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], Bytes({0xC1, 0x03, 0xF4}));
}

// La cabecera de un C2 son tres bytes: con los dos primeros espera el tercero, y no lo inventa.
TEST(FrameCutterTest, WaitsForTheSecondSizeByteOfAC2)
{
    FrameCutter cutter;

    EXPECT_TRUE(FeedAll(cutter, Bytes({0xC2, 0x00})).empty());
    EXPECT_FALSE(cutter.Error().has_value());

    const std::vector<Frame> frames = FeedAll(cutter, Bytes({0x04, 0xF4}));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], Bytes({0xC2, 0x00, 0x04, 0xF4}));
}

// Caso 3: un primer byte que no es C1 a C4 es un error. El original descarta todo el buffer,
// incluso el paquete bueno de atrás; el corte se para, y el paquete de atrás tampoco sale.
TEST(FrameCutterTest, StopsOnAnInvalidType)
{
    FrameCutter cutter;

    const std::vector<Frame> frames = FeedAll(cutter, Join(Bytes({0x00, 0x05, 0x01, 0x02, 0x03}), FirstC1));

    EXPECT_TRUE(frames.empty());
    EXPECT_EQ(cutter.Error(), CutError::InvalidType);
}

// Después de un error el corte queda parado: un paquete bien formado tampoco sale.
TEST(FrameCutterTest, StaysStoppedAfterAnError)
{
    FrameCutter cutter;
    ASSERT_TRUE(FeedAll(cutter, Bytes({0xC5})).empty());
    ASSERT_EQ(cutter.Error(), CutError::InvalidType);

    EXPECT_TRUE(FeedAll(cutter, FirstC1).empty());
    EXPECT_EQ(cutter.Error(), CutError::InvalidType);
    EXPECT_EQ(cutter.Pending(), 0u);
}

// Caso 4: con tamaño 0 el original no avanza y el buffer se llena hasta el "overflow". Acá es un
// error en cuanto llega el byte del tamaño, sin esperar más.
TEST(FrameCutterTest, StopsOnSizeZero)
{
    FrameCutter cutter;

    EXPECT_TRUE(FeedAll(cutter, Bytes({0xC1, 0x00})).empty());
    EXPECT_EQ(cutter.Error(), CutError::SizeTooSmall);
}

// El tamaño cuenta el paquete entero: un C1 necesita 3 (cabecera de 2 más el opcode) y un C2 4.
TEST(FrameCutterTest, StopsOnASizeSmallerThanHeaderAndOpcode)
{
    FrameCutter c1;
    EXPECT_TRUE(FeedAll(c1, Bytes({0xC1, 0x02})).empty());
    EXPECT_EQ(c1.Error(), CutError::SizeTooSmall);

    FrameCutter c2;
    EXPECT_TRUE(FeedAll(c2, Bytes({0xC2, 0x00, 0x03})).empty());
    EXPECT_EQ(c2.Error(), CutError::SizeTooSmall);

    FrameCutter c4;
    EXPECT_TRUE(FeedAll(c4, Bytes({0xC4, 0x00, 0x03})).empty());
    EXPECT_EQ(c4.Error(), CutError::SizeTooSmall);
}

// Los más chicos que valen: un C1 de 3 y un C2 de 4, solo la cabecera y el opcode.
TEST(FrameCutterTest, CutsTheSmallestValidPackets)
{
    FrameCutter cutter;

    const std::vector<Frame> frames = FeedAll(cutter, Bytes({0xC1, 0x03, 0xF4, 0xC2, 0x00, 0x04, 0xF4}));

    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0], Bytes({0xC1, 0x03, 0xF4}));
    EXPECT_EQ(frames[1], Bytes({0xC2, 0x00, 0x04, 0xF4}));
}

// El tamaño de un C2/C4 es big-endian: 01 2C son 300 bytes, no 0x2C01.
TEST(FrameCutterTest, ReadsTheC4SizeBigEndian)
{
    FrameCutter cutter;
    Frame packet = Bytes({0xC4, 0x01, 0x2C});
    packet.resize(300, std::byte{0x5A});

    const std::vector<Frame> frames = FeedAll(cutter, packet);

    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0], packet);
}

// Un número de 0 a n - 1 tomado de la salida del motor (engine() % n). El estándar fija los números
// de std::mt19937, pero no los de std::uniform_int_distribution, que cambian entre bibliotecas. La
// salida de std::mt19937 tiene 32 bits, así que entra en un std::uint32_t.
std::uint32_t Below(std::mt19937& engine, std::uint32_t n)
{
    return static_cast<std::uint32_t>(engine() % n);
}

// Un paquete bien formado al azar: tipo C1 a C4, tamaño desde la cabecera más el opcode, y el
// cuerpo con bytes cualquiera (el corte no lo mira).
Frame RandomPacket(std::mt19937& engine)
{
    // Tipos posibles, y el tamaño más grande: 255 para C1/C3 (un byte), y para C2/C4 uno que pasa
    // de 255 para que el byte alto del tamaño no sea siempre 0.
    constexpr std::byte Types[] = {Labs::TypeC1, Labs::TypeC2, Labs::TypeC3, Labs::TypeC4};
    constexpr std::uint32_t MaxOneByteSize = 0xFF;
    constexpr std::uint32_t MaxTwoByteSize = 600;

    const std::byte type = Types[Below(engine, static_cast<std::uint32_t>(std::size(Types)))];
    const bool oneByteSize = type == Labs::TypeC1 || type == Labs::TypeC3;
    const std::size_t headerSize = oneByteSize ? OneByteSizeHeader : TwoByteSizeHeader;
    const std::uint32_t maxSize = oneByteSize ? MaxOneByteSize : MaxTwoByteSize;
    const auto minSize = static_cast<std::uint32_t>(headerSize + OpcodeSize);
    const std::uint32_t size = minSize + Below(engine, maxSize - minSize + 1);

    Frame packet(size);
    packet[0] = type;
    if (oneByteSize)
    {
        packet[1] = static_cast<std::byte>(size);
    }
    else
    {
        packet[1] = static_cast<std::byte>(size >> 8);
        packet[2] = static_cast<std::byte>(size & 0xFF);
    }
    for (std::size_t i = headerSize; i < packet.size(); ++i)
    {
        packet[i] = static_cast<std::byte>(Below(engine, 0x100));
    }
    return packet;
}

// Propiedad: un flujo de paquetes al azar, de los cuatro tipos y de tamaños de todo el rango, sale
// en los mismos paquetes y en el mismo orden. No se parte en lecturas: el corte recibe de a un byte,
// así que partirlo no cambia nada. Esa propiedad es la del PacketReader de la CL2.1, que recibe
// lecturas enteras (PacketReaderTest.RandomSplitsGiveTheSamePackets).
TEST(FrameCutterTest, RandomPacketsComeOutInOrder)
{
    // Semilla fija: si el test falla, falla igual en todas las máquinas.
    constexpr std::mt19937::result_type Seed = 20261102;
    // Cuántos paquetes lleva el flujo.
    constexpr std::uint32_t PacketCount = 200;

    std::mt19937 engine(Seed);
    std::vector<Frame> expected;
    Frame stream;
    for (std::uint32_t k = 0; k < PacketCount; ++k)
    {
        expected.push_back(RandomPacket(engine));
        stream.insert(stream.end(), expected.back().begin(), expected.back().end());
    }

    FrameCutter cutter;
    const std::vector<Frame> got = FeedAll(cutter, stream);

    EXPECT_EQ(got, expected);
    EXPECT_EQ(cutter.Pending(), 0u);
    EXPECT_FALSE(cutter.Error().has_value());
}
} // namespace
