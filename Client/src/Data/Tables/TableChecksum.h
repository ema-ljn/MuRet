// El checksum de Item.bmd y Skill.bmd (Client/docs/research/05-formatos-de-datos.md, "Tablas
// .bmd"; lo calculan los cargadores de 0x54b9e0 y 0x54acf0). Va en el archivo, después de la
// tabla, y se calcula sobre la tabla tal como está ahí, cifrada:
//
//   acc = key << 9
//   por cada dword i (little-endian): acc ^= dword o acc += dword, según la paridad de i
//   si i % 4 == 0: acc ^= (acc + key) >> ((i & 7) + 1)
//
// Las cuentas son en 32 bits sin signo, que dan la vuelta (módulo 2^32) como en el original. Los
// bytes que sobran después del último dword completo no cuentan.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Data::Tables
{

// Qué dwords entran con XOR; los demás se suman.
enum class ChecksumParity
{
    XorOddDwords,  // Item.bmd
    XorEvenDwords, // Skill.bmd
};

// 05 § Tablas .bmd.
inline constexpr std::uint32_t ItemChecksumKey = 0xE2F1;
inline constexpr std::uint32_t SkillChecksumKey = 0x5A18;

[[nodiscard]] std::uint32_t TableChecksum(std::span<const std::byte> bytes, std::uint32_t key, ChecksumParity parity);

} // namespace Client::Data::Tables
