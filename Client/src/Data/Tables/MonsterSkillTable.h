#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

// Data\Local\MonsterSkill.bmd, las skills de los monstruos de [0x7437670]
// (Client/docs/research/05-formatos-de-datos.md, "MonsterSkill.bmd"; en el original, 0x54a440, que la carga de los
// datos del juego, 0x61ccb0, llama en 0x61ecdf con "Data\Local\MonsterSkill.bmd", 0x6d29fc).
//
// 0x54a440 pone en −1 solo los seis dwords del tipo 0 (0x54a44f..0x54a461); el resto de la tabla es .bss, en 0. Lee la
// cuenta en claro (un int32) y cuenta registros de 0x1c con fread en un bloque de operator new, les pasa el XOR3 desde
// su primer byte (0x5496c0, la clave de 0x6c8c74) y copia las cinco skills a 0x7437670 + tipo · 0x18 y el sexto int32
// a 0x7437684 + tipo · 0x18 (0x54a4b5..0x54a4d6). No mira cuánto leyó ni el tipo: un archivo corto deja en el bloque lo
// del registro anterior (en MuRet, FileTooShort) y un tipo fuera de la tabla escribe fuera de ella (en MuRet,
// InvalidData, fuera de [0, 0xffff], lo que cabe en +0x376). Sin el archivo, avisa con un MessageBox y cierra la
// ventana (0x54a500..0x54a549). *Verificado* con el del repo: 228 bytes, 8 registros, los tipos 0x130, 0x131, 0x135,
// 0x154, 0x155, 0x158, 0x159 y 0x15d. Los lee 0x4f92f0 (CL9 hito 3, fase 2c).
namespace Client::Data::Tables
{
inline constexpr std::size_t MonsterSkillRecordSize = 0x1c;
inline constexpr std::size_t MonsterSkillCount = 5;

// Lo de un tipo en 0x7437670 + tipo · 0x18.
struct MonsterSkills
{
    std::array<std::int32_t, MonsterSkillCount> skills{};
    // +0x14: el último int32 del registro; quién lo lee, sin leer.
    std::int32_t dword14 = 0;
};

struct MonsterSkillTable
{
    // Los tipos que trajo el archivo, con el último registro de cada uno (0x54a440 los copia en orden).
    std::map<std::uint16_t, MonsterSkills> records;

    // Lo que hay en la tabla para `kind`: lo del archivo; sin él, la .bss (0), salvo el tipo 0, en −1.
    [[nodiscard]] MonsterSkills Of(std::uint16_t kind) const;
};

// Descifra `file` como 0x54a440: la cuenta y cuenta registros. Con la cuenta en 0 o negativa, ninguno.
//   - Si faltan bytes para la cuenta o para un registro, FileTooShort.
//   - Un tipo fuera de [0, 0xffff], InvalidData.
//   - Lo que sobra se ignora.
Core::Expected<MonsterSkillTable, DataError> LoadMonsterSkillTable(std::span<const std::byte> file);
} // namespace Client::Data::Tables
