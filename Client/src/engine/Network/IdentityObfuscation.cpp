#include "Network/IdentityObfuscation.h"

#include "Core/Expected.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"

#include <cstddef>
#include <span>

namespace Client::Network
{
// A static local is built the first time it is asked for, even by another static object: it does not depend on the
// order files start in.
const IdentityObfuscation& IdentityObfuscation::Shared()
{
    static const IdentityObfuscation shared;
    return shared;
}

std::byte IdentityObfuscation::InboundMask(std::byte /*firstWireByte*/) const
{
    return std::byte{0};
}

Core::Expected<Packet, ObfuscationError> IdentityObfuscation::AfterCut(Packet packet) const
{
    return packet;
}

Core::Expected<Packet, ObfuscationError> IdentityObfuscation::BeforeDispatch(Packet packet) const
{
    return packet;
}

void IdentityObfuscation::BeforeEncrypt(std::span<std::byte> /*sealed*/) const {}

Packet IdentityObfuscation::BeforeSend(Packet packet) const
{
    return packet;
}
} // namespace Client::Network
