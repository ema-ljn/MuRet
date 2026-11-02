// Lo que puede fallar al armar o cortar un paquete C1/C2/C3/C4.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción", y el plan de la CL2
// (CL2.1). InvalidType, SizeTooSmall y TooLarge corrigen tres bugs del original: Q-C38, Q-C39 y
// Q-C05 en 07-quirks-y-bugs.md.

#pragma once

#include <cstdint>

namespace Client::Network
{

enum class PacketError : std::uint8_t
{
    InvalidType,  // el primer byte no es C1, C2, C3 ni C4
    SizeTooSmall, // menos que la cabecera y el opcode: 3 bytes en C1 y C3, 4 en C2 y C4
    SizeMismatch, // el tamaño que declara la cabecera no es el que mide el paquete
    TooLarge,     // más de MaxPacketSize (Q-C05)
};

} // namespace Client::Network
