// Client::Protocol::Connection, el login: el pedido F1 01 contra los fixtures de mu-client, su
// respuesta y el aviso F1 03. Solo con las credenciales sintéticas de mu-client.

#include "Protocol/Connection/Login.h"

#include "Network/Crypto/Xor32.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using Client::Network::EncryptPacket;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ClientSerial;
using Client::Protocol::ClientVersion;
using Client::Protocol::Delivery;
using Client::Protocol::Outgoing;
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::CredentialSize;
using Client::Protocol::Connection::LoginAccepted;
using Client::Protocol::Connection::LoginRequest;
using Client::Protocol::Connection::ReadLoginResult;
using Client::Protocol::Connection::WriteDecryptErrorReport;
using Client::Protocol::Connection::WriteLoginRequest;
using Client::Tests::Bytes;
using Client::Tests::BytesOf;
using Client::Tests::ErrorOf;
using Client::Tests::FixtureTick;
using Client::Tests::IdentityPair;
using Client::Tests::KeyPair;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// Dónde va cada campo del login (04, "F1 01"; PMSG_IDPASS en el research del servidor).
constexpr std::size_t AccountOffset = 4;
constexpr std::size_t PasswordOffset = 14;
constexpr std::size_t TickOffset = 24;
constexpr std::size_t VersionOffset = 28;
constexpr std::size_t SerialOffset = 33;
constexpr std::size_t LoginSize = 49;

// Las credenciales sintéticas de FIX_LOGIN_PLAIN y FIX_LOGIN_FULL_PLAIN.
constexpr LoginRequest ShortCredentials{"user0001", "pass0002", FixtureTick};
constexpr LoginRequest FullCredentials{"abcdefghij", "KLMNOPQRST", FixtureTick};

// Un paquete que tiene que salir: si WriteLoginRequest lo rechaza, el test falla.
Outgoing Written(const LoginRequest& request)
{
    auto written = WriteLoginRequest(request);
    EXPECT_TRUE(written.HasValue());
    return std::move(written).Value();
}

// Lo que sale al cable con la clave identidad, el serial 0 y el relleno en 0.
ByteVector EncryptedWithIdentity(const Packet& packet)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;
    const auto encrypted = EncryptPacket(packet, identity.encryption, serials);
    EXPECT_TRUE(encrypted.HasValue());
    if (!encrypted.HasValue())
    {
        return {};
    }
    const auto bytes = encrypted.Value().Bytes();
    return {bytes.begin(), bytes.end()};
}

// FIX_LOGIN_PLAIN y su C3: una cuenta y una clave de 8 bytes, completadas con dos \0, que con el
// XOR3 quedan en AB FC (los bytes 8 y 9 del campo, 8 % 3 = 2 y 9 % 3 = 0).
TEST(LoginTest, WritesTheLoginOfMuClient)
{
    const Outgoing login = Written(ShortCredentials);

    EXPECT_EQ(login.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(login.packet.Bytes(), Client::Tests::LoginPlain()));
    EXPECT_TRUE(SameBytes(EncryptedWithIdentity(login.packet), Client::Tests::LoginC3Serial0()));
}

// FIX_LOGIN_FULL_PLAIN y su C3: 10 bytes justos, sin \0.
TEST(LoginTest, WritesALoginWithFullCredentials)
{
    const Outgoing login = Written(FullCredentials);

    EXPECT_EQ(login.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(login.packet.Bytes(), Client::Tests::LoginFullPlain()));
    EXPECT_TRUE(SameBytes(EncryptedWithIdentity(login.packet), Client::Tests::LoginFullC3Serial0()));
}

// Sin el XOR32, cada campo en su lugar: el XOR3 vuelve a empezar en la clave, el tick va en
// little-endian, y la versión y el serial son los del build.
TEST(LoginTest, PutsEachFieldAtItsOffset)
{
    const Packet plain = Xor32Decode(Written(FullCredentials).packet);
    const std::span<const std::byte> bytes = plain.Bytes();

    ASSERT_EQ(bytes.size(), LoginSize);
    EXPECT_TRUE(SameBytes(bytes.first(AccountOffset), Bytes({0xC1, 0x31, 0xF1, 0x01})));
    EXPECT_TRUE(SameBytes(bytes.subspan(AccountOffset, 3), Bytes({'a' ^ 0xFC, 'b' ^ 0xCF, 'c' ^ 0xAB})));
    EXPECT_TRUE(SameBytes(bytes.subspan(PasswordOffset, 3), Bytes({'K' ^ 0xFC, 'L' ^ 0xCF, 'M' ^ 0xAB})));
    EXPECT_TRUE(SameBytes(bytes.subspan(TickOffset, 4), Bytes({0x44, 0x33, 0x22, 0x11})));
    EXPECT_TRUE(SameBytes(bytes.subspan(VersionOffset, ClientVersion.size()), BytesOf(ClientVersion)));
    EXPECT_TRUE(SameBytes(bytes.subspan(SerialOffset), BytesOf(ClientSerial)));
}

// Una cuenta o una clave que no entra en sus 10 bytes es FieldTooLong: no se trunca.
TEST(LoginTest, ACredentialOfMoreThanTenBytesIsFieldTooLong)
{
    const std::string_view eleven = "abcdefghijk";
    ASSERT_EQ(eleven.size(), CredentialSize + 1);

    EXPECT_EQ(ErrorOf(WriteLoginRequest({eleven, "pass0002", FixtureTick})), ProtocolError::FieldTooLong);
    EXPECT_EQ(ErrorOf(WriteLoginRequest({"user0001", eleven, FixtureTick})), ProtocolError::FieldTooLong);
}

// Una cuenta vacía deja en el campo los 10 bytes de la clave del XOR3, porque el XOR va sobre el
// campo entero, \0 incluidos. El original no llega a mandarla: 0x641bf0 corta antes si la cuenta o
// la clave están vacías.
TEST(LoginTest, AnEmptyAccountLeavesTheXor3KeyInTheField)
{
    const Packet plain = Xor32Decode(Written({"", "pass0002", FixtureTick}).packet);

    EXPECT_TRUE(SameBytes(plain.Bytes().subspan(AccountOffset, CredentialSize),
                          Bytes({0xFC, 0xCF, 0xAB, 0xFC, 0xCF, 0xAB, 0xFC, 0xCF, 0xAB, 0xFC})));
}

// Con la clave vacía pasa lo mismo, en su campo: el XOR3 vuelve a empezar ahí. El original tampoco
// la manda.
TEST(LoginTest, AnEmptyPasswordLeavesTheXor3KeyInTheField)
{
    const Packet plain = Xor32Decode(Written({"user0001", "", FixtureTick}).packet);

    EXPECT_TRUE(SameBytes(plain.Bytes().subspan(PasswordOffset, CredentialSize),
                          Bytes({0xFC, 0xCF, 0xAB, 0xFC, 0xCF, 0xAB, 0xFC, 0xCF, 0xAB, 0xFC})));
}

// La respuesta: C1 05 F1 01 y el resultado; 1 es aceptado. Los otros pasan tal cual, sin
// juntarlos en "no aceptado": 0x4e2b10 tiene una rama para cada uno de 0 a 5, y qué hacer con
// ellos es de la CL4.
TEST(LoginTest, ReadsTheLoginResult)
{
    const auto accepted = ReadLoginResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01})));
    const auto other = ReadLoginResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x00})));
    const auto three = ReadLoginResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x03})));

    ASSERT_TRUE(accepted.HasValue());
    EXPECT_EQ(accepted.Value(), LoginAccepted);
    ASSERT_TRUE(other.HasValue());
    EXPECT_EQ(other.Value(), 0);
    ASSERT_TRUE(three.HasValue());
    EXPECT_EQ(three.Value(), 3);
}

// Con un byte de más detrás del resultado se lee igual: lo que sobra no se mira (Q-C42).
TEST(LoginTest, ALoginResultWithTrailingBytesIsReadTheSame)
{
    const auto result = ReadLoginResult(PacketOf(Bytes({0xC1, 0x06, 0xF1, 0x01, 0x01, 0x00})));

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), LoginAccepted);
}

// Sin el resultado es TooShort, y otro F1 u otro opcode, WrongPacket.
TEST(LoginTest, OnlyAWholeF101IsALoginResult)
{
    EXPECT_EQ(ErrorOf(ReadLoginResult(PacketOf(Bytes({0xC1, 0x04, 0xF1, 0x01})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadLoginResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x02, 0x01})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadLoginResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x01, 0x01})))), ProtocolError::WrongPacket);
}

// El aviso: FIX_F1_PLAIN, y cifrado, FIX_C3_SERIAL0.
TEST(LoginTest, WritesTheDecryptErrorReport)
{
    const Outgoing report = WriteDecryptErrorReport();

    EXPECT_EQ(report.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(report.packet.Bytes(), Client::Tests::NoticeBytes()));
    EXPECT_TRUE(SameBytes(EncryptedWithIdentity(report.packet), Client::Tests::NoticeSerial0()));
}

} // namespace
