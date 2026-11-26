#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Data\Local\NpcName(%s).txt, the monster and NPC names (in the original 0x54e7f0 with the tokenizer 0x54e8d0, which
// the game data load, 0x61ccb0, calls at 0x61ed05 with "Data\Local\NpcName(%s).txt" and [0x5763350]'s language). It
// leaves them at 0x7681eb8: 512 records of 0x36 bytes, the type (a word) and the name.
// The tokenizer reads one byte at a time with getc (0x6a0641) and skips whitespace (isspace) and "//" comments up to
// '\n'. By the first byte: '"' is a string up to the other quote (no escapes), at 0x7681db0; a digit, '.' or '-' is a
// number (the digits, '.' and '-' that follow, atof to a float at 0x76baac4; the terminating byte is lost); a letter
// is a name (letters, digits, '.' and '_'; the terminator goes back with ungetc), also at 0x7681db0; '#', ',', ';',
// '{' and '}' are themselves, and anything else 0x3c. End of file, and byte 0xff (getc compares it as a char), is 2.
// 0x54e7f0: while the token is neither 2 nor the name "end", one more record with the type in the ftol of the last
// number read (whatever the token), and two more tokens: the name is the string the second left (strcpy; "" if it
// was something else). The lab file's quotes, number and name: " 84 1 \"King Orc\"".
namespace Client::Tables
{
// The records of 0x7681eb8..0x7688ab8.
inline constexpr std::size_t NpcNameCount = 512;
// 0x36 bytes minus the type's word.
inline constexpr std::size_t NpcNameBytes = 0x34;
// 0x7681db0..0x7681eb8: the token's string, which goes on into the table.
inline constexpr std::size_t NpcNameTokenBytes = 0x108;
// [ebp − 0x64]: the token's number, on 0x54e8d0's stack.
inline constexpr std::size_t NpcNameNumberBytes = 0x64;

struct NpcNameRecord
{
    // +0: the monster's or NPC's type.
    std::uint16_t type = 0;
    // +2: the name with its '\0' and then what was there (0: the table is in the .bss).
    std::array<char, NpcNameBytes> name{};

    friend bool operator==(const NpcNameRecord&, const NpcNameRecord&) = default;
};

// In 0x54e7f0's order. A name that does not fit its 0x34 bytes with its '\0' goes on, like the original's strcpy,
// into the type and name of the following records, which the next record overwrites with its own; those only that
// name touches after the last one read are returned too. InvalidData where the original would write outside the
// table or never finish: a name that would pass 0x7688ab8, more than 512 records (0x7688ab8 follows), a string or
// name of 0x108 bytes or more, a number of 0x64 or more, a comment reaching the end of the file without '\n'
// (0x54e925 never exits), a byte from 0x80 to 0xfe where the tokenizer asks isspace, isdigit or isalnum (signed char,
// outside the CRT's table) and an ftol outside int64. The last number starts at 0 (the .bss; *inferred*: in the
// release build no other tokenizer loader runs before). Bytes 0 to 0x7f are classified as in the "C" locale.
[[nodiscard]] Core::Expected<std::vector<NpcNameRecord>, Data::DataError> LoadNpcNames(std::span<const std::byte> file);
} // namespace Client::Tables
