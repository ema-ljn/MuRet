// Item.bmd, la tabla de items (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"; en el
// original, el cargador de 0x54b9e0). Se lee de Data\Local\Item.bmd.
//
// 8192 registros de 0x54 bytes, cada uno cifrado con XOR3, y después el checksum de los registros
// cifrados (TableChecksum.h, clave 0xE2F1).
//
// Q-C01: el original calcula el checksum pero no lo mira (un parche EB 66 saltea la comparación),
// y la item.bmd del laboratorio no pasa el suyo. MuRet lo calcula, lo informa y carga la tabla
// igual.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

namespace Client::Data::Tables
{

inline constexpr std::size_t ItemCount = 8192;
inline constexpr std::size_t ItemRecordSize = 0x54;
inline constexpr std::size_t ItemTableSize = ItemCount * ItemRecordSize; // 0xA8000

using ItemTable = RecordTable<ItemRecordSize>;

struct LoadedItemTable
{
    ItemTable table;
    // Si el checksum que guarda el archivo es el de sus registros. Con false (no coincide, o el
    // archivo no llega a guardarlo) la tabla se carga igual (Q-C01): quien la carga lo avisa en
    // el log.
    bool checksumMatches = false;
};

// Descifra los primeros ItemTableSize bytes de `file` y compara el checksum que les sigue.
//   - Si `file` no llega a tener todos los registros, devuelve DataError::FileTooShort (Q-C24). El
//     original leería igual y dejaría en la tabla lo que hubiera en la memoria.
//   - Si tiene los registros pero no los 4 bytes del checksum, carga igual, con checksumMatches
//     en false: el original tampoco mira el checksum.
//   - Si es más largo, el resto se ignora, como con los fread de tamaño fijo del original.
Core::Expected<LoadedItemTable, DataError> LoadItemTable(std::span<const std::byte> file);

} // namespace Client::Data::Tables
