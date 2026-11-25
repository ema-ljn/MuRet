#include "Network/SystemResolver.h"

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"

#include <optional>
#include <string>
#include <string_view>

namespace Client::Network
{
Core::Expected<Ipv4Address, SocketError> SystemResolver::Resolve(std::string_view host)
{
    if (host.empty() || host.find('\0') != std::string_view::npos)
    {
        return Core::Unexpected(SocketError::ResolveFailed);
    }
    // inet_pton y getaddrinfo leen un texto terminado en 0.
    const std::string text(host);
    if (const std::optional<Ipv4Address> ip = SocketApi::ParseIpv4(text))
    {
        return *ip;
    }
    if (const std::optional<Ipv4Address> ip = SocketApi::LookUpIpv4(text))
    {
        return *ip;
    }
    return Core::Unexpected(SocketError::ResolveFailed);
}
} // namespace Client::Network
