// Lo que puede fallar al cargar una clave del SimpleModulus, el cifrado de los C3/C4, y al cifrar o
// descifrar un paquete con ella.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4", y el plan de la CL2 (CL2.3).
// InvalidKey corrige un bug del original: Q-C41 en 07-quirks-y-bugs.md. De los del descifrado,
// ChecksumMismatch es donde el original también mira (0x685b30 devuelve -1), y NotWholeBlocks,
// InvalidBlock y TooShort, donde sigue sin mirar (03, "C3/C4").

#pragma once

#include <cstdint>

namespace Client::Network::Crypto
{

enum class CryptoError : std::uint8_t
{
    InvalidKeyFile,   // otro magic, otro tamaño declarado, o menos bytes que los que declara
    InvalidKey,       // un módulo en 0 o mayor que MaxModulus, o un XOR que llega al módulo (Q-C41)
    WrongPacketType,  // un C3/C4 para cifrar, un C1/C2 para descifrar, o un Packet sin sus bytes
    ChecksumMismatch, // el checksum de un bloque no coincide con sus 8 bytes descifrados
    InvalidBlock,     // un bloque dice 0 o más de 8 bytes útiles, o uno que no es el último, menos
    NotWholeBlocks,   // lo cifrado no mide un múltiplo de 11
    TooShort,         // lo descifrado no trae el serial y el opcode
    TooLarge,         // cifrado, el paquete pasaría de MaxPacketSize
};

} // namespace Client::Network::Crypto
