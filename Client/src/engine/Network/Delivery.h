#pragma once

#include <cstdint>

// Contract: Client/docs/research/04-protocolo.md, "Qué capa toca cada paquete". Protocol picks it, since it knows
// which packet goes encrypted (Protocol/Outgoing.h), and the Connection, which has the key, uses it.
namespace Client::Network
{
enum class Delivery : std::uint8_t
{
    Clear,     // C1/C2 to the wire as it is
    Encrypted, // C3/C4: encrypted with EncryptPacket
};
} // namespace Client::Network
