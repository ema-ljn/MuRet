// Un paquete del protocolo listo para salir, y cómo sale.
// Contrato: Client/docs/research/04-protocolo.md, "Qué capa toca cada paquete", y el plan de la CL2
// (CL2.5).
//
// Protocol arma el C1 con el XOR32 ya aplicado si lo lleva, y dice si va al cable así o cifrado
// como C3/C4 con EncryptPacket. Quien lo manda es la conexión de la CL4, que tiene la clave y los
// seriales.

#pragma once

#include "Network/Packet.h"

#include <cstdint>

namespace Client::Protocol
{

enum class Delivery : std::uint8_t
{
    Clear,     // C1/C2 al cable, tal como está
    Encrypted, // C3/C4: se cifra con EncryptPacket
};

struct Outgoing
{
    Network::Packet packet; // con el XOR32 ya aplicado si lo lleva
    Delivery delivery;
};

} // namespace Client::Protocol
