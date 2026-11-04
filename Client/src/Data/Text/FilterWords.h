#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// FilterName.bmd y Filter.bmd, las palabras que no puede tener el nombre de un personaje nuevo
// (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"; en el original, 0x549fa0 y
// 0x549c60, desde la carga de los datos del juego, 0x61ccb0).
//
// 20.000 bytes en 1.000 registros de 20, cada uno con el XOR3 de Text.bmd desde su primer byte
// (0x5496c0 por registro), y un uint32 con la suma de los 20.000 al final. La lectura para en el
// primer registro que empieza en cero. *Verificado*: las dos sumas del laboratorio dan, FilterName
// trae seis palabras y Filter ninguna.
namespace Client::Data::Text
{
inline constexpr std::size_t FilterRecordSize = 20;
inline constexpr std::size_t FilterTableSize = 20000;

// La semilla de la suma y lo que se le suma antes del corrimiento, por archivo.
struct FilterChecksumKey
{
    std::uint32_t seed = 0;
    std::uint32_t step = 0;
};

inline constexpr FilterChecksumKey FilterNameKey{0x578200, 0x2bc1}; // 0x549fa0
inline constexpr FilterChecksumKey FilterKey{0x7cfa00, 0x3e7d};     // 0x549c60

// La suma de los dos lectores, sobre el archivo cifrado: por cada palabra de 32 bits k (en
// little-endian), s += w con k par y s ^= w con k impar; y con k múltiplo de 4, después,
// s ^= (s + step) >> ((k & 7) + 1).
[[nodiscard]] std::uint32_t FilterChecksum(std::span<const std::byte, FilterTableSize> table, FilterChecksumKey key);

// Las palabras de `file`, descifradas, hasta el primer registro vacío; cada una hasta su primer
// cero, o de 20 bytes si lo llena. FileTooShort con menos de 20.004 bytes; ChecksumMismatch si la
// suma no es la de `key` (el original avisa "File corrupted" y se cierra). Lo que sobra se ignora.
[[nodiscard]] Core::Expected<std::vector<std::string>, DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                                  FilterChecksumKey key);
} // namespace Client::Data::Text
