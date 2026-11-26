#pragma once

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/SocketError.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Gives `address`, or ResolveFailed if it is empty, without DNS: the DNS can be slow or rewrite an NXDOMAIN.
namespace Client::Tests
{
class FakeResolver final : public Network::IResolver
{
public:
    std::optional<Network::Ipv4Address> address;
    std::vector<std::string> hosts; // what it was asked, in order

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
