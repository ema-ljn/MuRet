// Client::Protocol::Connection, el keepalive 0E: el ajuste de las velocidades y los cinco fixtures
// de mu-client.

#include "Protocol/Connection/Keepalive.h"

#include "Network/Crypto/Xor32.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetworkPrinters.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using Client::Network::EncryptPacket;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Delivery;
using Client::Protocol::Outgoing;
using Client::Protocol::Connection::Keepalive;
using Client::Protocol::Connection::KeepaliveAdjust20;
using Client::Protocol::Connection::KeepaliveAdjust25;
using Client::Protocol::Connection::KeepaliveSpeed;
using Client::Protocol::Connection::WriteKeepalive;
using Client::Tests::Adjust25WrapTick;
using Client::Tests::Bytes;
using Client::Tests::FixtureTick;
using Client::Tests::IdentityPair;
using Client::Tests::KeyPair;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// Sin los bits de ajuste, la velocidad va como está.
constexpr std::uint8_t NoAdjust = 0x00;

// Los dos bits: gana KeepaliveAdjust20.
constexpr std::uint8_t BothAdjusts = KeepaliveAdjust20 | KeepaliveAdjust25;

// Unos seriales que ya mandaron `sent` paquetes: el próximo sale con ese serial.
PacketSerials HavingSent(int sent)
{
    PacketSerials serials;
    for (int k = 0; k < sent; ++k)
    {
        [[maybe_unused]] const std::uint8_t serial = serials.TakeSend();
    }
    return serials;
}

// Con KeepaliveAdjust20 resta 20, con KeepaliveAdjust25 resta 25, y con los dos gana el de 20.
TEST(KeepaliveTest, AdjustsTheSpeedWithTheBitsOfTheCharacter)
{
    EXPECT_EQ(KeepaliveSpeed(40, NoAdjust), 40);
    EXPECT_EQ(KeepaliveSpeed(40, KeepaliveAdjust20), 20);
    EXPECT_EQ(KeepaliveSpeed(40, KeepaliveAdjust25), 15);
    EXPECT_EQ(KeepaliveSpeed(40, BothAdjusts), 20);
}

// Los otros seis bits no cambian nada, ni solos ni con uno de los dos.
TEST(KeepaliveTest, TheOtherBitsDoNotAdjust)
{
    constexpr std::uint8_t Others = static_cast<std::uint8_t>(~BothAdjusts);

    EXPECT_EQ(KeepaliveSpeed(0x0328, Others), 0x0328);
    EXPECT_EQ(KeepaliveSpeed(0x0328, static_cast<std::uint8_t>(Others | KeepaliveAdjust25)), 0x0328 - 25);
    EXPECT_EQ(KeepaliveSpeed(0x0328, static_cast<std::uint8_t>(Others | KeepaliveAdjust20)), 0x0328 - 20);
}

// La resta da la vuelta en 16 bits, como el WORD del original: 3 - 25 es 0xFFEA, y 20 - 20, 0.
TEST(KeepaliveTest, TheAdjustmentWrapsIn16Bits)
{
    EXPECT_EQ(KeepaliveSpeed(3, KeepaliveAdjust25), 0xFFEA);
    EXPECT_EQ(KeepaliveSpeed(19, KeepaliveAdjust20), 0xFFFF);
    EXPECT_EQ(KeepaliveSpeed(20, KeepaliveAdjust20), 0);
    EXPECT_EQ(KeepaliveSpeed(0xFFFF, KeepaliveAdjust25), 0xFFFF - 25);
}

// Un fixture de mu-client: lo que tenía el personaje, cómo sale con su XOR32 y cómo sale cifrado.
struct KeepaliveFixture
{
    std::string_view name;
    std::uint32_t tick = 0;
    std::uint16_t attackSpeed = 0;
    std::uint16_t magicSpeed = 0;
    std::uint8_t characterFlags = 0;
    int serial = 0;
    std::byte padding{};
    ByteVector plain;
    ByteVector encrypted;
};

// Los cinco, con el ajuste aplicado por KeepaliveSpeed y cifrados con la clave identidad.
TEST(KeepaliveTest, WritesTheFiveKeepalivesOfMuClient)
{
    const std::vector<KeepaliveFixture> fixtures{
        {"RAW", FixtureTick, 0x0017, 0x0016, NoAdjust, 0, std::byte{0}, Client::Tests::KeepaliveRawPlain(),
         Client::Tests::KeepaliveRawC3Serial0()},
        {"ADJUST20", FixtureTick, 40, 50, KeepaliveAdjust20, 0, std::byte{0}, Client::Tests::KeepaliveAdjust20Plain(),
         Client::Tests::KeepaliveAdjust20C3Serial0()},
        {"ADJUST25_WRAP", Adjust25WrapTick, 3, 2, KeepaliveAdjust25, 0, std::byte{0},
         Client::Tests::KeepaliveAdjust25WrapPlain(), Client::Tests::KeepaliveAdjust25WrapC3Serial0()},
        {"ADJUST20_PRIORITY", FixtureTick, 40, 50, BothAdjusts, 0, std::byte{0},
         Client::Tests::KeepaliveAdjust20Plain(), Client::Tests::KeepaliveAdjust20C3Serial0()},
        {"HIGH_Y", FixtureTick, 0x0017, 0x1234, NoAdjust, 255, std::byte{0xA5}, Client::Tests::KeepaliveHighYPlain(),
         Client::Tests::KeepaliveHighYC3SerialFFRandA5()},
    };
    const KeyPair identity = IdentityPair();

    for (const KeepaliveFixture& fixture : fixtures)
    {
        const Outgoing keepalive =
            WriteKeepalive({fixture.tick, KeepaliveSpeed(fixture.attackSpeed, fixture.characterFlags),
                            KeepaliveSpeed(fixture.magicSpeed, fixture.characterFlags)});
        PacketSerials serials = HavingSent(fixture.serial);
        const auto encrypted = EncryptPacket(keepalive.packet, identity.encryption, serials, fixture.padding);

        EXPECT_EQ(keepalive.delivery, Delivery::Encrypted) << fixture.name;
        EXPECT_TRUE(SameBytes(keepalive.packet.Bytes(), fixture.plain)) << fixture.name;
        ASSERT_TRUE(encrypted.HasValue()) << fixture.name;
        EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), fixture.encrypted)) << fixture.name;
    }
}

// Sin el XOR32: C1 0C 0E 00, el tick en +4 y las velocidades en +8 y +10, todo en little-endian,
// donde las lee el GameServer.
TEST(KeepaliveTest, PutsEachFieldAtItsOffset)
{
    const Packet plain = Xor32Decode(WriteKeepalive({0xA0B0C0D0, 0x0328, 0x0268}).packet);

    EXPECT_TRUE(
        SameBytes(plain.Bytes(), Bytes({0xC1, 0x0C, 0x0E, 0x00, 0xD0, 0xC0, 0xB0, 0xA0, 0x28, 0x03, 0x68, 0x02})));
}

} // namespace
