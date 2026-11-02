#pragma once

#include "Network/Packet.h"
#include "Network/PacketWriter.h"

#include <cstdint>

// Un paquete del protocolo listo para salir, y cómo sale.
// Contrato: Client/docs/research/04-protocolo.md, "Qué capa toca cada paquete", y el plan de la CL2
// (CL2.5).
//
// Protocol arma el C1 con el XOR32 ya aplicado si lo lleva, y dice si va al cable así o cifrado
// como C3/C4 con EncryptPacket. Quien lo manda es la conexión de la CL4, que tiene la clave y los
// seriales.
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

// Terminar un C1 que arma un emisor del protocolo y envolverlo en un Outgoing. Son para los
// paquetes de largo fijo, que siempre entran en un C1 (Network::MaxC1Size): Finish no falla, y si
// fallara sería un bug del emisor, y Value() corta el programa.

// En claro, sin el XOR32, como los F4 que van al ConnectServer.
[[nodiscard]] Outgoing ClearC1(const Network::PacketWriter& writer);

// Con el XOR32 y para cifrar como C3, como el login y el keepalive.
[[nodiscard]] Outgoing EncryptedC1(const Network::PacketWriter& writer);
} // namespace Client::Protocol
