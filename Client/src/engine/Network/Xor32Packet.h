#pragma once

#include "Network/Packet.h"

// XOR32 over a whole packet, on top of Network/Crypto/Xor32.h's bytes (03 § Sending: the C1/C2 engine and XOR32).
namespace Client::Network
{
// A C1 or a C2 from the byte after the opcode. A C3 or a C4 comes back as it is: what it carries is encrypted before
// it is wrapped (CL2.3).
[[nodiscard]] Packet Xor32Encode(Packet packet);
[[nodiscard]] Packet Xor32Decode(Packet packet);
} // namespace Client::Network
