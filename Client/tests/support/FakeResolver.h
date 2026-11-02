#pragma once

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/SocketError.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Un IResolver que da `address`, o ResolveFailed si está vacío, sin DNS: el de los tests, porque el
// DNS puede tardar o reescribir un NXDOMAIN.
namespace Client::Tests
{
class FakeResolver final : public Network::IResolver
{
public:
    std::optional<Network::Ipv4Address> address;
    std::vector<std::string> hosts; // lo que le preguntaron, en orden

    [[nodiscard]] Core::Expected<Network::Ipv4Address, Network::SocketError> Resolve(std::string_view host) override
    {
        hosts.emplace_back(host);
        if (!address)
        {
            return Core::Unexpected(Network::SocketError::ResolveFailed);
        }
        return *address;
    }
};
} // namespace Client::Tests
