#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Las tablas de los conjuntos de ítems: ItemSetType.bmd e ItemSetOption.bmd
// (Client/docs/research/05-formatos-de-datos.md, "Las tablas de los conjuntos"). En el original las
// carga 0x40c600 (0x40c660 y 0x40c850) en el objeto estático de 0x6e5930, al que apunta [0x6eed3c]:
// los tipos desde +4 y los conjuntos desde +0x8004. Se leen de Data\Local\ItemSetType.bmd y
// Data\Local\ItemSetOption.bmd; las variantes *Test.bmd son de 0x40c600 con su argumento distinto de 0,
// que el original no usa (0x61ecd5 lo llama con 0).
//
// Cada archivo trae sus registros, cada uno cifrado con XOR3, y después el checksum de los registros
// cifrados (TableChecksum.h, con la paridad de Item.bmd). El original sí lo mira: si no coincide,
// muestra "File corrupted." y no toca la tabla.
namespace Client::Data::Tables
{
// ItemSetType.bmd: un registro de 4 bytes por tipo de ítem, el mismo índice que Item.bmd. Los bytes
// 0 a 2 son el conjunto del ítem con el byte +0x1c & 3 en 1, 2 o 3 (0x40cfa0); 0 o 0xff, ninguno.
inline constexpr std::size_t ItemSetTypeCount = ItemCount;
inline constexpr std::size_t ItemSetTypeRecordSize = 4;
inline constexpr std::size_t ItemSetTypeTableSize = ItemSetTypeCount * ItemSetTypeRecordSize;
static_assert(ItemSetTypeTableSize == 0x8000, "lo que lee 0x40c660 (05, Las tablas de los conjuntos)");
// La clave del checksum (0x40c6bb y 0x40c6ea).
inline constexpr std::uint32_t ItemSetTypeChecksumKey = 0xE5F1;

// ItemSetOption.bmd: 64 conjuntos de 0x4c bytes, con el nombre, las opciones por cantidad de piezas,
// las del conjunto completo y las marcas de las clases (06, "Los stats del registro propio").
inline constexpr std::size_t ItemSetOptionCount = 0x40;
inline constexpr std::size_t ItemSetOptionRecordSize = 0x4c;
inline constexpr std::size_t ItemSetOptionTableSize = ItemSetOptionCount * ItemSetOptionRecordSize;
static_assert(ItemSetOptionTableSize == 0x1300, "lo que lee 0x40c850 (05, Las tablas de los conjuntos)");
// La clave del checksum (0x40c8ae y 0x40c8dd).
inline constexpr std::uint32_t ItemSetOptionChecksumKey = 0xA2F1;

using ItemSetTypeTable = RecordTable<ItemSetTypeRecordSize>;
using ItemSetOptionTable = RecordTable<ItemSetOptionRecordSize>;

// Las dos tablas de [0x6eed3c], en claro.
struct ItemSetTables
{
    ItemSetTypeTable types;
    ItemSetOptionTable options;
};

// Descifran los primeros ItemSetTypeTableSize o ItemSetOptionTableSize bytes de `file`, si el
// checksum que les sigue es el suyo, como Skill.bmd:
//   - si no coincide, DataError::ChecksumMismatch y no hay tabla ("File corrupted.");
//   - si `file` no llega a tener los registros y los 4 bytes del checksum, DataError::FileTooShort
//     (Q-C24: el original leería igual, con el heap sin inicializar en lo que falta);
//   - si es más largo, el resto se ignora, como con los fread de tamaño fijo del original.
Core::Expected<ItemSetTypeTable, DataError> LoadItemSetTypeTable(std::span<const std::byte> file);
Core::Expected<ItemSetOptionTable, DataError> LoadItemSetOptionTable(std::span<const std::byte> file);
} // namespace Client::Data::Tables
