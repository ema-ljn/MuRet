// K16, el cifrado de los archivos de terreno EncTerrain<N>.map, .att y .obj
// (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; en el original va escrito
// dentro de cada cargador: 0x5f87b0, 0x5f8390 y 0x607370).
//
// A diferencia de XOR3, tiene estado: cada byte se descifra con una clave de 16 bytes y con lo que
// dejó el byte cifrado anterior.
//
//   plano[i] = (K16Key[i % 16] ^ cifrado[i]) - previo
//   previo   = cifrado[i] + K16Step          (para el primer byte, K16Seed)
//
// Las cuentas son en 8 bits sin signo, que dan la vuelta. Es un solo flujo sobre el archivo
// entero: `previo` no vuelve a empezar entre la cabecera y las capas, y el byte de la clave va
// por la posición en el archivo, no en la capa. Como `previo` sale del byte cifrado, que está en
// el archivo, un pedazo del medio se puede descifrar sin lo anterior: alcanza con saber dónde
// empieza y cuál es el byte cifrado de antes. DecryptK16 no hace eso: descifra desde el principio.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Data::Terrain
{

// La clave: los 16 bytes que cada cargador del original arma en la pila, uno por uno.
inline constexpr std::array<std::byte, 16> K16Key = {
    std::byte{0xD1}, std::byte{0x73}, std::byte{0x52}, std::byte{0xF6}, std::byte{0xD2}, std::byte{0x9A},
    std::byte{0xCB}, std::byte{0x27}, std::byte{0x3E}, std::byte{0xAF}, std::byte{0x59}, std::byte{0x31},
    std::byte{0x37}, std::byte{0xB3}, std::byte{0xE7}, std::byte{0xA2},
};
// El `previo` del primer byte del archivo (0x5f84a9 en el cargador del .att, 0x5f886e en el del
// .map).
inline constexpr std::uint8_t K16Seed = 0x5E;
// Lo que se le suma a un byte cifrado para que sea el `previo` del siguiente (0x5f84d3 y 0x5f88a0).
inline constexpr std::uint8_t K16Step = 0x3D;

// Descifra `bytes` en el lugar. `bytes` tiene que ser el archivo desde su primer byte.
void DecryptK16(std::span<std::byte> bytes);

} // namespace Client::Data::Terrain
