#include "Network/SystemResolver.h"

#include "Network/IResolver.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <string_view>

// Without DNS. A real name and a nonexistent one (`nombre.invalid`) are left to the manual test: they depend on the
// DNS.
namespace
{
using Client::Network::Ipv4Address;
using Client::Network::SocketError;
using Client::Network::SystemResolver;
using Client::Network::SocketApi::NetworkInit;
using Client::Tests::ErrorOf;

using namespace std::string_view_literals;

TEST(SystemResolverTest, AnIpv4IsReadInTheOrderItIsWritten)
{
    const NetworkInit network;
    SystemResolver resolver;

    EXPECT_EQ(resolver.Resolve("127.0.0.1").Value(), (Ipv4Address{127, 0, 0, 1}));
    EXPECT_EQ(resolver.Resolve("192.168.10.200").Value(), (Ipv4Address{192, 168, 10, 200}));
}

TEST(SystemResolverTest, AnEmptyHostFails)
{
    const NetworkInit network;
    SystemResolver resolver;

    EXPECT_EQ(ErrorOf(resolver.Resolve("")), SocketError::ResolveFailed);
}

// The system reads up to the first 0: "127.0.0.1\0x" would resolve 127.0.0.1.
TEST(SystemResolverTest, AHostWithAZeroByteFails)
{
    const NetworkInit network;
    SystemResolver resolver;

    EXPECT_EQ(ErrorOf(resolver.Resolve("127.0.0.1\0x"sv)), SocketError::ResolveFailed);
}
} // namespace
