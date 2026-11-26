#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

// Gate.bmd (05-formatos-de-datos.md, "Tablas .bmd"; in the original 0x54a360's loader), read from Data\Gate.bmd, not
// Data\Local\. 0x900 bytes: 256 records of 9, no header or checksum, each encrypted with XOR3.
namespace Client::Tables
{
// A gate is a crossing between maps (the server has its own in gate.txt). The original always reads 256.
inline constexpr std::size_t GateCount = 256;
// In the file and in the original's memory. GateRecord names its bytes.
inline constexpr std::size_t GateRecordSize = 9;
// The whole file: no header or checksum.
inline constexpr std::size_t GateTableSize = GateCount * GateRecordSize;
static_assert(GateTableSize == 0x900, "el tamaño que mide el research (05, Tablas .bmd)");

using GateTable = Data::Tables::RecordTable<GateRecordSize>;

// FileTooShort if `file` is shorter. The original does not check the size: it reads each record over the previous
// one's buffer, and the missing ones come from what was left there (0054a360-gate-bmd-loader.md §2), a bug fixed here
// (Q-C24). A longer file's rest is ignored, as in the original.
Core::Expected<GateTable, Data::DataError> LoadGateTable(std::span<const std::byte> file);
} // namespace Client::Tables
