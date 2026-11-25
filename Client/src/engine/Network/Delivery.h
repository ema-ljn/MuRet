#pragma once

#include <cstdint>

// Cómo sale un paquete al cable: tal como está, o cifrado como C3/C4.
// Contrato: Client/docs/research/04-protocolo.md, "Qué capa toca cada paquete", y el plan de la CL4
// (CL4.3).
//
// Lo elige Protocol, que sabe qué paquete va cifrado (Protocol/Outgoing.h), y lo usa la Connection,
// que tiene la clave. Venía en Protocol/Outgoing.h (CL2.5) y bajó a Network porque Network no ve a
// Protocol.
namespace Client::Network
{
enum class Delivery : std::uint8_t
{
    Clear,     // C1/C2 al cable, tal como está
    Encrypted, // C3/C4: se cifra con EncryptPacket
};
} // namespace Client::Network
