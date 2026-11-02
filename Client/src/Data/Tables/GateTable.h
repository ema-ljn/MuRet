#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

// Gate.bmd, la tabla de gates (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"; en el
// original, el cargador de 0x54a360). Se lee de Data\Gate.bmd, no de Data\Local\.
//
// 0x900 bytes: 256 registros de 9, sin cabecera ni checksum, cada uno cifrado con XOR3.
namespace Client::Data::Tables
{
// Cuántos gates trae la tabla, uno por registro. Un gate es un punto de paso entre mapas (el
// servidor tiene los suyos en su gate.txt). El original lee siempre 256.
inline constexpr std::size_t GateCount = 256;
// Lo que ocupa cada gate, en el archivo y en la memoria del original. Qué es cada byte se lee en
// la CL9.
inline constexpr std::size_t GateRecordSize = 9;
// La tabla entera, que es el archivo entero: no hay cabecera ni checksum.
inline constexpr std::size_t GateTableSize = GateCount * GateRecordSize;
static_assert(GateTableSize == 0x900, "el tamaño que mide el research (05, Tablas .bmd)");

using GateTable = RecordTable<GateRecordSize>;

// Descifra los primeros GateTableSize bytes de `file`.
//   - Si `file` es más corto, devuelve DataError::FileTooShort. El original no mira el tamaño:
//     lee cada registro sobre el buffer del anterior, y los que faltan salen de lo que quedó en él
//     (0054a360-gate-bmd-loader.md §2). Es estado que queda corrido, un bug interno, y se corrige
//     (Q-C24 en Client/docs/research/07-quirks-y-bugs.md).
//   - Si es más largo, el resto se ignora, como en el original.
Core::Expected<GateTable, DataError> LoadGateTable(std::span<const std::byte> file);
} // namespace Client::Data::Tables
