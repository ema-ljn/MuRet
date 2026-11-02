#include "Network/SystemResolver.h"

#include "Network/IResolver.h"
#include "Network/SocketApi.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <string_view>

// Client::Network::SystemResolver, sin DNS: una IPv4 en texto no pregunta a nadie, y lo que no
// puede ser un nombre falla antes de preguntar. Un nombre de verdad, y uno que no existe
// (`nombre.invalid`), quedan para la prueba a mano: dependen del DNS (el plan de la CL4,
// "Qué no se prueba en CI").
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

// El sistema lee el texto hasta el primer 0: "127.0.0.1\0x" resolvería 127.0.0.1.
TEST(SystemResolverTest, AHostWithAZeroByteFails)
{
    const NetworkInit network;
    SystemResolver resolver;

    EXPECT_EQ(ErrorOf(resolver.Resolve("127.0.0.1\0x"sv)), SocketError::ResolveFailed);
}
} // namespace
