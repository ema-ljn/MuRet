#include "Protocol/JewelMix.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>

// El BC de la mezcla de joyas (CL9 hito 6): 0x402500 y 0x401f30, con el XOR32 desde pkt[3] y en claro.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Tests::Bytes;
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
} // namespace
