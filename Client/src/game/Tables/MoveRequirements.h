#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Data\Local\movereq.bmd, the /move window's destinations (05-formatos-de-datos.md, "movereq.bmd"; in the original
// 0x448440 from 0x449830, which the game data load, 0x61ccb0, calls at 0x61ec8f with "Data\Local\movereq.bmd").
// 0x448440 reads the count (a plain int) and, for each, a 0x50-byte record with fread into an operator new block,
// runs XOR3 from its first byte (0x449770, FC CF AB with the key on the stack) and appends it to 0x723c68's list (+8,
// the count at +0xc). No checksum, and it does not check how much it read. *Verified* with the lab's: 28
// destinations, then 640 unread bytes (eight zero records).
namespace Client::Tables
{
inline constexpr std::size_t MoveCountSize = 4;
inline constexpr std::size_t MoveRecordSize = 0x50;
inline constexpr std::size_t MoveNameSize = 0x20;

struct MoveRequirement
{
    // +0: the destination's number.
    std::int32_t index = 0;
    // +4: the name the window draws (0x623690) and says with text 260 (0x4487d7). Up to its first '\0', which may be in
    // +0x24's: the block goes on.
    std::string name;
    // +0x24, raw: the one 0x4497c0 compares (with strcmp, to return the zen), which nobody calls.
    std::array<char, MoveNameSize> searchName{};
    // +0x44: the minimum level (signed: 0x448580 and 0x448fa0 compare it with JL and JG).
    std::int32_t level = 0;
    // +0x48: unsigned in the comparison; signed in the drawing's _itoa.
    std::uint32_t zen = 0;
    // +0x4c: the Gate.bmd gate, which the client does not read (the server moves with /move and the name).
    std::int32_t gate = 0;

    friend bool operator==(const MoveRequirement&, const MoveRequirement&) = default;
};

// In file order. The count comes from the first four bytes (those present: fread leaves at 0 the missing ones of the
// int 0x448440 zeroed); with 0 or less, none. FileTooShort if a counted record is missing (the original would fill it
// with what the new block had); InvalidData if a name does not end before its record's end (strlen would go past the
// block). The excess is ignored.
[[nodiscard]] Core::Expected<std::vector<MoveRequirement>, Data::DataError>
LoadMoveRequirements(std::span<const std::byte> file);
} // namespace Client::Tables
