#include "Protocol/Connection/Hello.h"

#include "Protocol/BuildConstants.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

// Client::Protocol::Connection::ReadServerHello: el saludo F1 00 del GameServer.
namespace
{
using Client::Protocol::ClientVersion;
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::HelloAccepted;
using Client::Protocol::Connection::ReadServerHello;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// La versión de un saludo, como texto para compararla.
std::string_view VersionOf(const Client::Protocol::Connection::ServerHello& hello)
{
    return {hello.version.data(), hello.version.size()};
}

// El paso 1 de la CL2.5, a mano, campo por campo:
//   C1     el tipo: un C1, con el tamaño en un byte y sin cifrar;
//   0C     el tamaño del paquete entero, la cabecera incluida: 12 bytes, los que hay;
//   F1     el opcode, el de la conexión al GameServer, que tiene subcódigo;
//   00     el subcódigo, pkt[3] en un C1: el 00 es el saludo;
//   01     el resultado: 1 acepta la conexión (con otro, 0x4c1e20 muestra "Connectting error");
//   00 05  el índice del jugador, en big-endian, el byte más significativo primero: 0x4c1e20 hace
//          pkt[5] * 0x100 + pkt[6], que da 0 * 256 + 5 = 5. Leído en little-endian, como los
//          enteros que manda el cliente, daría 0x0500, 1.280;
//   31 30 32 30 39  la versión, en ASCII y sin \0: "10209", la de este build. 0x4c1e20 la compara
//          byte a byte con la suya, que guarda en 0x6c8284 con cada byte más su índice más 1.
TEST(HelloTest, ReadsTheHelloDecodedByHand)
{
    const auto hello =
        ReadServerHello(PacketOf(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x01, 0x00, 0x05, 0x31, 0x30, 0x32, 0x30, 0x39})));

    ASSERT_TRUE(hello.HasValue());
    EXPECT_EQ(hello.Value().result, HelloAccepted);
    EXPECT_EQ(hello.Value().playerIndex, 5);
    EXPECT_EQ(VersionOf(hello.Value()), "10209");
    EXPECT_EQ(VersionOf(hello.Value()), ClientVersion);
}

// Con los dos bytes del índice distintos de 0 se ve el orden entero: 12 C0 es 0x12C0, 4.800.
TEST(HelloTest, ThePlayerIndexIsBigEndian)
{
    const auto hello =
        ReadServerHello(PacketOf(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x01, 0x12, 0xC0, 0x30, 0x39, 0x39, 0x30, 0x38})));

    ASSERT_TRUE(hello.HasValue());
    EXPECT_EQ(hello.Value().playerIndex, 0x12C0);
    EXPECT_EQ(VersionOf(hello.Value()), "09908");
}

// Un resultado que no acepta se lee igual, tal cual, sin pasarlo a 0: qué hacer con él es de quien
// lo usa (CL4).
TEST(HelloTest, ReadsAResultThatIsNotAccepted)
{
    const auto hello =
        ReadServerHello(PacketOf(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x31, 0x30, 0x32, 0x30, 0x39})));
    const auto two =
        ReadServerHello(PacketOf(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x02, 0x00, 0x00, 0x31, 0x30, 0x32, 0x30, 0x39})));

    ASSERT_TRUE(hello.HasValue());
    EXPECT_EQ(hello.Value().result, 0);
    ASSERT_TRUE(two.HasValue());
    EXPECT_EQ(two.Value().result, 2);
}

// Sin la versión entera es TooShort; con un byte de más se lee igual, y lo que sobra no se lee.
TEST(HelloTest, AHelloWithoutItsWholeVersionIsTooShort)
{
    std::vector<std::byte> hello = Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x01, 0x00, 0x05, 0x31, 0x30, 0x32, 0x30, 0x39});
    std::vector<std::byte> longer = hello;
    longer.push_back(std::byte{0x00});
    longer[1] = static_cast<std::byte>(longer.size());
    hello.pop_back();
    hello[1] = static_cast<std::byte>(hello.size());

    EXPECT_EQ(ErrorOf(ReadServerHello(PacketOf(hello))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadServerHello(PacketOf(Bytes({0xC1, 0x04, 0xF1, 0x00})))), ProtocolError::TooShort);
    EXPECT_TRUE(ReadServerHello(PacketOf(longer)).HasValue());
}

// La versión son cinco bytes, incluso NUL o bits altos; no es una cadena ni incluye la cola.
TEST(HelloTest, PreservesEveryVersionByteAndIgnoresATrailingByte)
{
    const auto exact =
        ReadServerHello(PacketOf(Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x02, 0x34, 0xA0, 0x00, 0x7F, 0x80, 0xFF, 0x39})));
    const auto withTail = ReadServerHello(
        PacketOf(Bytes({0xC1, 0x0D, 0xF1, 0x00, 0x02, 0x34, 0xA0, 0x00, 0x7F, 0x80, 0xFF, 0x39, 0xA5})));
    ASSERT_TRUE(exact.HasValue());
    ASSERT_TRUE(withTail.HasValue());
    EXPECT_EQ(exact.Value().result, 2);
    EXPECT_EQ(withTail.Value().result, 2);
    EXPECT_EQ(exact.Value().playerIndex, 0x34A0);
    EXPECT_EQ(withTail.Value().playerIndex, 0x34A0);
    constexpr std::array<unsigned char, Client::Protocol::VersionSize> expected{0x00, 0x7F, 0x80, 0xFF, 0x39};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(static_cast<unsigned char>(exact.Value().version[index]), expected[index]);
        EXPECT_EQ(static_cast<unsigned char>(withTail.Value().version[index]), expected[index]);
    }
}

// El saludo es un C1 F1 00: la respuesta del login (F1 01) o un F3 00 no lo son.
TEST(HelloTest, AnotherPacketIsNotAHello)
{
    EXPECT_EQ(ErrorOf(ReadServerHello(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadServerHello(
                  PacketOf(Bytes({0xC1, 0x0C, 0xF3, 0x00, 0x01, 0x00, 0x05, 0x31, 0x30, 0x32, 0x30, 0x39})))),
              ProtocolError::WrongPacket);
}
} // namespace
