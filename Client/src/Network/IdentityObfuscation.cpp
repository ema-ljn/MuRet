#include "Network/IdentityObfuscation.h"

namespace Client::Network
{

// Una static local se construye la primera vez que se pide, aunque la pida otro objeto static: no
// depende del orden en que arrancan los archivos.
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
