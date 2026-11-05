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
// (0x5496c0 por registro), y el checksum de las tablas de los 20.000, cifrados, al final. La lectura para en el
// primer registro que empieza en cero. *Verificado*: las dos sumas del laboratorio dan, FilterName
// trae seis palabras y Filter ninguna.
namespace Client::Data::Text
{
inline constexpr std::size_t FilterRecordSize = 20;
inline constexpr std::size_t FilterTableSize = 20000;

// Las claves de la suma (Data::Tables::TableChecksum con la paridad de Item.bmd: la semilla
// 0x578200 y 0x7cfa00 son la clave << 9).
inline constexpr std::uint32_t FilterNameChecksumKey = 0x2bc1; // 0x549fa0
inline constexpr std::uint32_t FilterChecksumKey = 0x3e7d;     // 0x549c60

// Las palabras de `file`, descifradas, hasta el primer registro vacío; cada una hasta su primer
// cero, o de 20 bytes si lo llena. Sin un registro vacío, ninguna (el original no guarda la cuenta). FileTooShort con
// menos de 20.004 bytes; ChecksumMismatch si la suma no es la de `checksumKey` (el original avisa "File corrupted" y se
// cierra). Lo que sobra se ignora.
[[nodiscard]] Core::Expected<std::vector<std::string>, DataError> LoadFilterWords(std::span<const std::byte> file,
                                                                                  std::uint32_t checksumKey);
} // namespace Client::Data::Text
