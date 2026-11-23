#include "Network/Xor32Packet.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"

#include <cstddef>

namespace Client::Network
{
namespace
{
// MutableBody() starts at the opcode, which stays in the clear.
constexpr std::size_t AfterTheOpcode = 1;

// Only C1 and C2 carry it at this level.
bool HasXor32(PacketType type)
{
    return type == PacketType::C1 || type == PacketType::C2;
}
} // namespace

// The body's byte j is the packet's byte HeaderSize + j, so the key is shifted by HeaderSize.
Packet Xor32Encode(Packet packet)
{
    if (HasXor32(packet.Type()))
    {
        Crypto::Xor32EncodeBytes(packet.MutableBody(), AfterTheOpcode, HeaderSize(packet.Type()));
    }
    return packet;
}

Packet Xor32Decode(Packet packet)
{
    if (HasXor32(packet.Type()))
    {
        Crypto::Xor32DecodeBytes(packet.MutableBody(), AfterTheOpcode, HeaderSize(packet.Type()));
    }
    return packet;
}
} // namespace Client::Network
