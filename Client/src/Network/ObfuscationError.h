// Lo que puede fallar en la capa de ofuscación (Network/Obfuscation.h).
// Contrato: Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti", y el plan de la CL2
// (CL2.4). InvalidPacket corrige los largos que el original no mira: Q-C09 en
// 07-quirks-y-bugs.md.

#pragma once

#include <cstdint>

namespace Client::Network
{

enum class ObfuscationError : std::uint8_t
{
    InvalidPacket, // un largo o una cantidad que no entra en el paquete, o lo traducido no entraría
};

} // namespace Client::Network
