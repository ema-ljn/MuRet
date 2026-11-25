#pragma once

#include <cstdint>

// Lo que puede fallar en la capa de ofuscación (Network/IObfuscation.h).
// Contrato: Client/docs/research/03-red-y-cifrado.md, "La capa de FoxAnti", y el plan de la CL2
// (CL2.4). InvalidPacket corrige los largos que el original no mira: Q-C09 en
// 07-quirks-y-bugs.md.
namespace Client::Network
{
enum class ObfuscationError : std::uint8_t
{
    // Un largo o una cantidad que no entra en el paquete, o lo traducido no entraría. La conexión
    // (CL4) descarta ese paquete y sigue leyendo: el corte no se perdió, porque el largo lo dio la
    // cabecera.
    InvalidPacket,
};
} // namespace Client::Network
