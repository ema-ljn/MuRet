#include "Protocol/Outgoing.h"

#include "Network/Delivery.h"
#include "Network/PacketWriter.h"
#include "Network/Xor32Packet.h"

namespace Client::Protocol
{
Outgoing ClearC1(const Network::PacketWriter& writer)
{
    return {writer.Finish().Value(), Network::Delivery::Clear};
}

Outgoing Xor32C1(const Network::PacketWriter& writer)
{
    return {Network::Xor32Encode(writer.Finish().Value()), Network::Delivery::Clear};
}

Outgoing EncryptedC1(const Network::PacketWriter& writer)
{
    return {Network::Xor32Encode(writer.Finish().Value()), Network::Delivery::Encrypted};
}
} // namespace Client::Protocol
