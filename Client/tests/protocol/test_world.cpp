#include "Protocol/World.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
using Client::Network::Delivery;
using Client::Network::EncryptPacket;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Protocol::World::ReadRegen;
using Client::Protocol::World::ReadTeleport;
using Client::Protocol::World::Regen;
using Client::Protocol::World::WriteCrywolfState;
using Client::Protocol::World::WriteGateRequest;
using Client::Protocol::World::WriteTeleportDone;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::IdentityPair;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// 04 § Los cambios de mapa: los cuatro C3 del servidor, ya descifrados a C1.
TEST(WorldProtocolTest, ReadsTheFourDocumentedTeleports)
{
    struct Case
    {
        int gate;
        int map;
        int x;
        int y;
    };
    constexpr std::array<Case, 4> Cases{
        {{0x11, 0x00, 0x8a, 0x79}, {0x1b, 0x03, 0xab, 0x6c}, {0x4b, 0x07, 0xe1, 0x35}, {0x11, 0x00, 0x90, 0x81}}};
    for (const auto& test : Cases)
    {
        const auto teleport = ReadTeleport(PacketOf(Bytes({0xc1, 8, 0x1c, test.gate, test.map, test.x, test.y, 0})));
        ASSERT_TRUE(teleport.HasValue());
        EXPECT_EQ(teleport.Value().gate, test.gate);
        EXPECT_EQ(teleport.Value().map, test.map);
        EXPECT_EQ(teleport.Value().x, test.x);
        EXPECT_EQ(teleport.Value().y, test.y);
        EXPECT_EQ(teleport.Value().direction, 0);
    }
}

// Valores sintéticos: el protocolo entrega bytes completos, sin convertir el yaw ni limitar mapas.
TEST(WorldProtocolTest, ReadsTeleportFieldsWithoutSwappingOrMaskingThem)
{
    const auto teleport = ReadTeleport(PacketOf(Bytes({0xc1, 8, 0x1c, 0, 0xfe, 0x12, 0xa7, 0xff})));
    ASSERT_TRUE(teleport.HasValue());
    EXPECT_EQ(teleport.Value().gate, 0);
    EXPECT_EQ(teleport.Value().map, 0xfe);
    EXPECT_EQ(teleport.Value().x, 0x12);
    EXPECT_EQ(teleport.Value().y, 0xa7);
    EXPECT_EQ(teleport.Value().direction, 0xff);
}

TEST(WorldProtocolTest, ReadsZeroAndMaximumTeleportFields)
{
    for (const int value : {0, 0xff})
    {
        const auto teleport = ReadTeleport(PacketOf(Bytes({0xc1, 8, 0x1c, value, value, value, value, value})));
        ASSERT_TRUE(teleport.HasValue());
        EXPECT_EQ(teleport.Value().gate, value);
        EXPECT_EQ(teleport.Value().map, value);
        EXPECT_EQ(teleport.Value().x, value);
        EXPECT_EQ(teleport.Value().y, value);
        EXPECT_EQ(teleport.Value().direction, value);
    }
}

TEST(WorldProtocolTest, EveryTruncatedTeleportIsTooShort)
{
    const auto full = Bytes({0xc1, 8, 0x1c, 1, 2, 3, 4, 5});
    for (std::size_t size = 3; size < full.size(); ++size)
    {
        std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
        truncated[1] = static_cast<std::byte>(size);
        EXPECT_EQ(ErrorOf(ReadTeleport(PacketOf(std::move(truncated)))), ProtocolError::TooShort) << size;
    }
}

TEST(WorldProtocolTest, TeleportRejectsWrongTypesAndOpcodes)
{
    for (auto bytes :
         {Bytes({0xc1, 3, 0x1d}), Bytes({0xc2, 0, 4, 0x1c}), Bytes({0xc3, 3, 0x1c}), Bytes({0xc4, 0, 4, 0x1c})})
    {
        EXPECT_EQ(ErrorOf(ReadTeleport(PacketOf(std::move(bytes)))), ProtocolError::WrongPacket);
    }
}

TEST(WorldProtocolTest, TeleportIgnoresTrailingFields)
{
    const auto teleport = ReadTeleport(PacketOf(Bytes({0xc1, 10, 0x1c, 0x11, 3, 4, 5, 6, 0xaa, 0xbb})));
    ASSERT_TRUE(teleport.HasValue());
    EXPECT_EQ(teleport.Value().gate, 0x11);
    EXPECT_EQ(teleport.Value().map, 3);
    EXPECT_EQ(teleport.Value().x, 4);
    EXPECT_EQ(teleport.Value().y, 5);
    EXPECT_EQ(teleport.Value().direction, 6);
}

std::vector<std::byte> AsymmetricRegen()
{
    return Bytes({0xc1, 0x18, 0xf3, 0x04, 0x12, 0x34, 0x56, 0xff, 0x78, 0x9a, 0xbc, 0xde,
                  0xf0, 0x13, 0xa5, 0x5a, 0x24, 0x68, 0xac, 0xe0, 0x35, 0x79, 0xbd, 0xf1});
}

// 0x4c4b80: MOV word/dword en 0x4c4c23..0x4c4c4a, sin swaps.
// +0xe/+0xf no se leen; la dirección se devuelve cruda, no se convierte a yaw.
TEST(WorldProtocolTest, ReadsRegenLittleEndianFieldsAndIgnoresNonzeroPadding)
{
    const auto regen = ReadRegen(PacketOf(AsymmetricRegen()));
    ASSERT_TRUE(regen.HasValue());
    EXPECT_EQ(regen.Value().x, 0x12);
    EXPECT_EQ(regen.Value().y, 0x34);
    EXPECT_EQ(regen.Value().map, 0x56);
    EXPECT_EQ(regen.Value().direction, 0xff);
    EXPECT_EQ(regen.Value().life, 0x9a78);
    EXPECT_EQ(regen.Value().mana, 0xdebc);
    EXPECT_EQ(regen.Value().bp, 0x13f0);
    EXPECT_EQ(regen.Value().experience, 0xe0ac6824u);
    EXPECT_EQ(regen.Value().zen, 0xf1bd7935u);
}

// 04 § Los cambios de mapa: los dos F3 04 del muproxy.log, filtrados con decode.py
// --heads f304 y descifrados a C1. Son solo campos numéricos; el relleno 1200/2000 no es cero.
TEST(WorldProtocolTest, ReadsTheTwoNativeRegenPackets)
{
    struct Case
    {
        std::vector<std::byte> bytes;
        Regen expected;
    };
    const std::array<Case, 2> Cases{{{Bytes({0xc1, 0x18, 0xf3, 0x04, 0xb2, 0x6a, 0x03, 0x06, 0x50, 0x00, 0x1e, 0x00,
                                             0x14, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}),
                                      {178, 106, 3, 6, 80, 30, 20, 0, 0}},
                                     {Bytes({0xc1, 0x18, 0xf3, 0x04, 0x94, 0x7f, 0x00, 0x03, 0x76, 0x01, 0x56, 0x00,
                                             0x3a, 0x00, 0x20, 0x00, 0xf4, 0xc7, 0xa1, 0x00, 0x5f, 0x0d, 0x00, 0x00}),
                                      {148, 127, 0, 3, 374, 86, 58, 10602484, 3423}}}};
    for (const auto& test : Cases)
    {
        const auto regen = ReadRegen(PacketOf(test.bytes));
        ASSERT_TRUE(regen.HasValue());
        EXPECT_EQ(regen.Value().x, test.expected.x);
        EXPECT_EQ(regen.Value().y, test.expected.y);
        EXPECT_EQ(regen.Value().map, test.expected.map);
        EXPECT_EQ(regen.Value().direction, test.expected.direction);
        EXPECT_EQ(regen.Value().life, test.expected.life);
        EXPECT_EQ(regen.Value().mana, test.expected.mana);
        EXPECT_EQ(regen.Value().bp, test.expected.bp);
        EXPECT_EQ(regen.Value().experience, test.expected.experience);
        EXPECT_EQ(regen.Value().zen, test.expected.zen);
    }
}

TEST(WorldProtocolTest, ReadsZeroAndMaximumRegenStatistics)
{
    for (const int value : {0, 0xff})
    {
        auto bytes = std::vector<std::byte>(24, static_cast<std::byte>(value));
        bytes[0] = std::byte{0xc1};
        bytes[1] = std::byte{24};
        bytes[2] = std::byte{0xf3};
        bytes[3] = std::byte{0x04};
        const auto regen = ReadRegen(PacketOf(std::move(bytes)));
        ASSERT_TRUE(regen.HasValue());
        EXPECT_EQ(regen.Value().x, value);
        EXPECT_EQ(regen.Value().y, value);
        EXPECT_EQ(regen.Value().map, value);
        EXPECT_EQ(regen.Value().direction, value);
        const std::uint16_t word = value == 0 ? 0 : 0xffff;
        const std::uint32_t dword = value == 0 ? 0 : 0xffffffffu;
        EXPECT_EQ(regen.Value().life, word);
        EXPECT_EQ(regen.Value().mana, word);
        EXPECT_EQ(regen.Value().bp, word);
        EXPECT_EQ(regen.Value().experience, dword);
        EXPECT_EQ(regen.Value().zen, dword);
    }
}

TEST(WorldProtocolTest, EveryTruncatedRegenIsTooShort)
{
    const auto full = AsymmetricRegen();
    for (std::size_t size = 3; size < full.size(); ++size)
    {
        std::vector<std::byte> truncated(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
        truncated[1] = static_cast<std::byte>(size);
        EXPECT_EQ(ErrorOf(ReadRegen(PacketOf(std::move(truncated)))), ProtocolError::TooShort) << size;
    }
}

TEST(WorldProtocolTest, RegenRejectsWrongTypesOpcodesAndSubcodes)
{
    for (auto bytes : {Bytes({0xc1, 4, 0xf2, 0x04}), Bytes({0xc1, 4, 0xf3, 0x03}), Bytes({0xc1, 4, 0xf3, 0x05}),
                       Bytes({0xc2, 0, 5, 0xf3, 0x04}), Bytes({0xc3, 4, 0xf3, 0x04}), Bytes({0xc4, 0, 5, 0xf3, 0x04})})
    {
        EXPECT_EQ(ErrorOf(ReadRegen(PacketOf(std::move(bytes)))), ProtocolError::WrongPacket);
    }
}

TEST(WorldProtocolTest, RegenIgnoresTrailingFields)
{
    auto bytes = AsymmetricRegen();
    bytes.push_back(std::byte{0xff});
    bytes[1] = static_cast<std::byte>(bytes.size());
    const auto regen = ReadRegen(PacketOf(std::move(bytes)));
    ASSERT_TRUE(regen.HasValue());
    EXPECT_EQ(regen.Value().experience, 0xe0ac6824u);
    EXPECT_EQ(regen.Value().zen, 0xf1bd7935u);
}

// 04 § Los cambios de mapa, 0x599ef0: expectativa estática del pedido, no hay cable nativo.
// Los bytes XOR32 se calcularon aparte con la tabla documentada de 03 § Envío.
TEST(WorldProtocolTest, GateRequestKeepsTheWholeGateByteAndIsXor32ForC3)
{
    struct Case
    {
        std::uint8_t gate;
        int first;
        int second;
        int third;
    };
    constexpr std::array<Case, 3> Cases{{{0, 0x95, 0x29, 0x9b}, {0x11, 0x84, 0x38, 0x8a}, {0xff, 0x6a, 0xd6, 0x64}}};
    for (const auto& test : Cases)
    {
        const auto request = WriteGateRequest(test.gate);
        EXPECT_EQ(request.delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xc1, 6, 0x1c, test.gate, 0, 0})));
        EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xc1, 6, 0x1c, test.first, test.second, test.third})));
    }
}

// Expectativa estática, no hay captura nativa del pedido. Clave identidad de los tests,
// serial42 y paddingA5: sealed=42 1c 84 38 8a a5 00 00, cinco bytes útiles.
// EncryptPacket pone un solo byte A5 tras los útiles; el resto del bloque sigue en cero.
// Primera pasada: 1c42/24c6/814c/814c; segunda: 3884/a58a/0000/814c.
// Los 18 bits por carril y checksum35 dan el bloque literal de abajo, sin usar EncryptPacket
// para formar la expectativa ni un roundtrip como oráculo.
TEST(WorldProtocolTest, GateRequestMatchesTheStaticC3WithFixedSerialAndPadding)
{
    const auto identity = IdentityPair();
    PacketSerials serials;
    constexpr int FixedSerial = 0x42;
    for (int sent = 0; sent < FixedSerial; ++sent)
    {
        [[maybe_unused]] const std::uint8_t discarded = serials.TakeSend();
    }
    const auto request = WriteGateRequest(0x11);
    EXPECT_EQ(request.delivery, Delivery::Encrypted);
    const auto encrypted = EncryptPacket(request.packet, identity.encryption, serials, std::byte{0xa5});
    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Value().Type(), PacketType::C3);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(),
                          Bytes({0xc3, 0x0d, 0x84, 0x38, 0x22, 0xa9, 0x40, 0x00, 0x01, 0x32, 0x04, 0x0d, 0x35})));
    EXPECT_EQ(serials.TakeSend(), 0x43);
}

// 04 § Los cambios de mapa: C1 04 F3 68 en el cable documentado, sin C3.
TEST(WorldProtocolTest, TeleportDoneMatchesTheNativeWireBytes)
{
    const auto done = WriteTeleportDone();
    EXPECT_EQ(done.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(done.packet).Bytes(), Bytes({0xc1, 4, 0xf3, 0x12})));
    EXPECT_TRUE(SameBytes(done.packet.Bytes(), Bytes({0xc1, 4, 0xf3, 0x68})));
}

// 04 § El clima y Crywolf, 0x4c4269/0x4c78ac: byte cero; evidencia estática.
TEST(WorldProtocolTest, CrywolfStateWritesZeroWithXor32AndWithoutC3)
{
    const auto request = WriteCrywolfState();
    EXPECT_EQ(request.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(request.packet).Bytes(), Bytes({0xc1, 4, 0xbd, 0})));
    EXPECT_TRUE(SameBytes(request.packet.Bytes(), Bytes({0xc1, 4, 0xbd, 0x34})));
}
} // namespace
