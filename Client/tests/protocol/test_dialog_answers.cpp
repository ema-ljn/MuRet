#include "Protocol/DialogAnswers.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>

// El AF 01 y el D0 00 de los diálogos (CL9 hito 6): 0x628594 y 0x62a480, con el XOR32 desde pkt[3] y en claro.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace DialogAnswers = Client::Protocol::DialogAnswers;

// 0x628594..0x628947: C1 06 AF 01, el nivel 3 y el lugar 0x14.
TEST(DialogAnswersProtocolTest, TheChaosCastleEntryIsAClearXor32C1WithTheLevelAndTheSlot)
{
    const auto entry = DialogAnswers::WriteChaosCastleEntry(3, 0x14);
    EXPECT_EQ(entry.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(entry.packet).Bytes(), Bytes({0xc1, 0x06, 0xaf, 0x01, 0x03, 0x14})));
    // 0x6286c4..0x6286e9: el subcódigo con la clave de pkt[3] (0x89) y el opcode, que no se cifra.
    EXPECT_EQ(entry.packet.Bytes()[3], std::byte{0x01 ^ 0x89 ^ 0xaf});
}

// 0x62a480..0x62a5e8: C1 04 D0 00.
TEST(DialogAnswersProtocolTest, TheNoticeYesIsAClearXor32C1)
{
    const auto yes = DialogAnswers::WriteNoticeYes();
    EXPECT_EQ(yes.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(yes.packet).Bytes(), Bytes({0xc1, 0x04, 0xd0, 0x00})));
    EXPECT_EQ(yes.packet.Bytes()[3], std::byte{0x00 ^ 0x89 ^ 0xd0});
}
} // namespace
