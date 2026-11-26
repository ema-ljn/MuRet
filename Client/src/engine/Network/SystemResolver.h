#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/IResolver.h"
#include "Network/SocketError.h"

#include <string_view>

// An IPv4 text is parsed without asking anyone (inet_pton); a name goes to the DNS with getaddrinfo and AF_INET,
// which waits, like 0x4e8570's gethostbyname. A name that does not resolve is ResolveFailed: the original goes on as
// if connected (returns 2 and 0x4c1340 only checks 0). Needs a live SocketApi::NetworkInit: on Windows getaddrinfo
// is Winsock's.
namespace Client::Network
{
class ENGINE_API SystemResolver final : public IResolver
{
public:
    // Empty or with a 0 byte inside is ResolveFailed without asking: the system reads up to the first 0 and would
    // resolve another name.
    [[nodiscard]] Core::Expected<Ipv4Address, SocketError> Resolve(std::string_view host) override;
};
} // namespace Client::Network
