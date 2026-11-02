// Lo que puede fallar al cargar una clave del SimpleModulus, el cifrado de los C3/C4.
// Contrato: Client/docs/research/03-red-y-cifrado.md, "C3/C4", y el plan de la CL2 (CL2.3).
// InvalidKey corrige un bug del original: Q-C41 en 07-quirks-y-bugs.md.

#pragma once

#include <cstdint>

namespace Client::Network::Crypto
{

enum class CryptoError : std::uint8_t
{
    InvalidKeyFile, // otro magic, otro tamaño declarado, o menos bytes que los que declara
    InvalidKey,     // un módulo en 0 o mayor que MaxModulus, o un XOR que llega al módulo (Q-C41)
};

} // namespace Client::Network::Crypto
