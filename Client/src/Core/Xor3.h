// XOR3: cada byte i se combina con XOR con FC, CF o AB según i % 3. Aplicarlo dos veces devuelve
// los bytes de antes, así que la misma función cifra y descifra.
//
// Lo usan las tablas .bmd y el terreno (Client/docs/research/05-formatos-de-datos.md, "Tablas
// .bmd"; en el original, 0x5496c0 con la clave de 0x6c8c74, y 0x5f8650 para el .att) y la cuenta y
// la clave del login (04-protocolo.md, "F1 01"; 0x4c1690 con la de 0x6c82e4). El original tiene
// cada copia con su clave, que son los mismos tres bytes; MuRet tiene una sola.
//
// i se cuenta desde el principio de `bytes`. Aplicarlo por partes da lo mismo que de una vez solo
// si cada parte arranca en un múltiplo de 3, como los registros de 300 bytes de Text.bmd. El login
// lo aplica a la cuenta y a la clave por separado, así que en cada campo vuelve a empezar en FC.

#pragma once

#include <cstddef>
#include <span>

namespace Client::Core
{

void Xor3(std::span<std::byte> bytes);

} // namespace Client::Core
