#include "Protocol/JewelMix.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>

// El BC de la mezcla de joyas (CL9 hito 6): 0x402500 y 0x401f30, con el XOR32 desde pkt[3] y en claro, y la
// respuesta que lee 0x4e4547.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace JewelMix = Client::Protocol::JewelMix;

// 0x402500: C1 06 BC 00, la joya del alma (1) y el paquete de 20 (1).
TEST(JewelMixProtocolTest, TheMixIsAClearXor32C1WithTheKindAndTheBundle)
{
    const auto mix = JewelMix::WriteJewelMix(1, 1);
    EXPECT_EQ(mix.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(mix.packet).Bytes(), Bytes({0xc1, 0x06, 0xbc, 0x00, 0x01, 0x01})));
    // 0x4025ba..: el subcódigo con la clave de pkt[3] (0x89) y el opcode, que no se cifra.
    EXPECT_EQ(mix.packet.Bytes()[3], std::byte{0x00 ^ 0x89 ^ 0xbc});
}

// 0x401f30: C1 07 BC 01, la joya de bendición (0), el nivel 2 y el lugar 0x1d (la casilla 17 + 12).
TEST(JewelMixProtocolTest, TheUnmixIsAClearXor32C1WithTheKindTheLevelAndTheSlot)
{
    const auto unmix = JewelMix::WriteJewelUnmix(0, 2, 0x1d);
    EXPECT_EQ(unmix.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(unmix.packet).Bytes(), Bytes({0xc1, 0x07, 0xbc, 0x01, 0x00, 0x02, 0x1d})));
}

// 0x4e4547: pkt[3] y, con 0 o 1, pkt[4]; con otro subcódigo pkt[4] no se lee.
TEST(JewelMixProtocolTest, ReadsTheAnswerSubcodeAndResult)
{
    const auto mixed = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x05, 0xbc, 0x00, 0x03})));
    ASSERT_TRUE(mixed.HasValue());
    EXPECT_EQ(mixed.Value().subcode, 0);
    EXPECT_EQ(mixed.Value().result, 3);
    const auto unmixed = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x05, 0xbc, 0x01, 0x08})));
    ASSERT_TRUE(unmixed.HasValue());
    EXPECT_EQ(unmixed.Value().subcode, 1);
    EXPECT_EQ(unmixed.Value().result, 8);
    const auto other = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x04, 0xbc, 0x02})));
    ASSERT_TRUE(other.HasValue());
    EXPECT_EQ(other.Value().subcode, 2);
    EXPECT_EQ(other.Value().result, 0);

    const auto noResult = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x04, 0xbc, 0x00})));
    ASSERT_FALSE(noResult.HasValue());
    EXPECT_EQ(noResult.Error(), ProtocolError::TooShort);
    const auto noSubcode = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x03, 0xbc})));
    ASSERT_FALSE(noSubcode.HasValue());
    EXPECT_EQ(noSubcode.Error(), ProtocolError::TooShort);
    const auto wrong = JewelMix::ReadJewelMixAnswer(PacketOf(Bytes({0xc1, 0x05, 0xbd, 0x00, 0x01})));
    ASSERT_FALSE(wrong.HasValue());
    EXPECT_EQ(wrong.Error(), ProtocolError::WrongPacket);
}
} // namespace
