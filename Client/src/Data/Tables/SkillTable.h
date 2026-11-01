// Skill.bmd, la tabla de skills (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"; en
// el original, el cargador de 0x54acf0). Se lee de Data\Local\Skill.bmd.
//
// 256 registros de 0x3C bytes, cada uno cifrado con XOR3, y después el checksum de los registros
// cifrados (TableChecksum.h, clave 0x5A18). A diferencia del de Item, el original sí lo mira.
//
// Q-C08: el original guarda los registros en un contenedor "guardia" que, mientras nadie los usa,
// deja en su lugar bytes de rand(). Es una protección contra trampas y no se porta: MuRet guarda
// la tabla en claro.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/RecordTable.h"

#include <cstddef>
#include <span>

namespace Client::Data::Tables
{

inline constexpr std::size_t SkillCount = 256;
inline constexpr std::size_t SkillRecordSize = 0x3C;
inline constexpr std::size_t SkillTableSize = SkillCount * SkillRecordSize; // 0x3C00

using SkillTable = RecordTable<SkillRecordSize>;

// Descifra los primeros SkillTableSize bytes de `file`, si el checksum que les sigue es el suyo.
//   - Si el checksum no coincide, devuelve DataError::ChecksumMismatch y no hay tabla, como en el
//     original ("File corrupted.").
//   - Si `file` no llega a tener los registros y los 4 bytes del checksum, devuelve
//     DataError::FileTooShort (Q-C24).
//   - Si es más largo, el resto se ignora, como con los fread de tamaño fijo del original.
Core::Expected<SkillTable, DataError> LoadSkillTable(std::span<const std::byte> file);

} // namespace Client::Data::Tables
