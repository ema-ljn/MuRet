// XOR3, el cifrado de las tablas .bmd (Client/docs/research/05-formatos-de-datos.md, "Tablas
// .bmd"; en el original, 0x5496c0).
//
// Cada byte i se combina con XOR con FC, CF o AB según i % 3 (la clave está en 0x6c8c74).
// Aplicarlo dos veces devuelve los bytes de antes, así que la misma función cifra y descifra.
//
// i se cuenta desde el principio de `bytes`. Aplicarlo por partes da lo mismo que de una vez solo
// si cada parte arranca en un múltiplo de 3, como los registros de 300 bytes de Text.bmd.

#pragma once

#include <cstddef>
#include <span>

namespace Client::Data::Tables
{

void Xor3(std::span<std::byte> bytes);

} // namespace Client::Data::Tables
