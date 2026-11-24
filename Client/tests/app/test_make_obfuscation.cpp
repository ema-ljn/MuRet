#include "App/MakeObfuscation.h"

#include "Network/IObfuscation.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

// MU_TEST_INBOUND_MASK comes from tests/app/CMakeLists.txt: 0xAA with MU_XOR_REMAP, 0 without.
namespace
{
using Client::App::MakeObfuscation;
using Client::Network::IObfuscation;

// A C1 that arrives with the lab server's XOR starts with 0x6B.
TEST(MakeObfuscationTest, BuildsTheLayerTheBuildChose)
{
    const std::unique_ptr<const IObfuscation> obfuscation = MakeObfuscation();

    ASSERT_NE(obfuscation, nullptr);
    EXPECT_EQ(obfuscation->InboundMask(std::byte{0x6B}), std::byte{MU_TEST_INBOUND_MASK});
}
} // namespace
