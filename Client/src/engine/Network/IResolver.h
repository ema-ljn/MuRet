#pragma once

#include "Core/Expected.h"
#include "Network/SocketError.h"

#include <array>
#include <cstdint>
#include <string_view>

// The system one (SystemResolver) waits while it asks the DNS, like 0x4e8570's gethostbyname; the tests' one
// (FakeResolver) answers what it is told, without DNS.
namespace Client::Network
{
// In writing order: 127.0.0.1 is {127, 0, 0, 1}.
using Ipv4Address = std::array<std::uint8_t, 4>;

class IResolver
{
public:
    virtual ~IResolver() = default;
    // ResolveFailed if there is none.
    [[nodiscard]] virtual Core::Expected<Ipv4Address, SocketError> Resolve(std::string_view host) = 0;

protected:
    // Like IObfuscation: copied and moved only from a derived class, so assigning through two IResolver& does not copy
    // only the base (slicing).
    IResolver() = default;
    IResolver(const IResolver&) = default;
    IResolver& operator=(const IResolver&) = default;
    IResolver(IResolver&&) = default;
    IResolver& operator=(IResolver&&) = default;
};
} // namespace Client::Network
