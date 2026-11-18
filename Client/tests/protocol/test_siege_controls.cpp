#include "Protocol/SiegeControls.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <utility>

namespace
{
namespace Controls = Client::Protocol::SiegeControls;
using Client::Network::Crypto::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

TEST(SiegeControlsTest, TheWeaponOrderUsesBigEndianKeyAndOneBasedChoice)
{
    auto packet = Controls::WriteWeapon(0xabcd, 3);
    EXPECT_EQ(packet.delivery, Client::Network::Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(std::move(packet.packet)).Bytes(), Bytes({0xc1, 7, 0xb7, 1, 0xab, 0xcd, 3})));
}
TEST(SiegeControlsTest, TheGateOrderPlacesTheActionBeforeItsBigEndianKey)
{
    auto packet = Controls::WriteGate(1, 0x1234);
    EXPECT_TRUE(SameBytes(Xor32Decode(std::move(packet.packet)).Bytes(), Bytes({0xc1, 7, 0xb2, 1, 1, 0x12, 0x34})));
}
TEST(SiegeControlsTest, TheCommanderSendsTheFourRawBytesWithoutRenumbering)
{
    auto packet = Controls::WriteCommand(7, 255, 0, 3);
    EXPECT_TRUE(SameBytes(Xor32Decode(std::move(packet.packet)).Bytes(), Bytes({0xc1, 8, 0xb2, 0x1d, 7, 255, 0, 3})));
}
} // namespace
