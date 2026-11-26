#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Data\Local\credit.bmd (05-formatos-de-datos.md, "credit.bmd"; in the original 0x63ff30 from 0x63f020, building the
// login UI). 0x63ff30 reads 13,200 bytes (0x3390) into the credits window's +0x610 and runs 0x4c1690, the login's
// XOR3, over them at once from the first byte. That gives 400 records of 33: the kind in the first byte (1 a part's
// title, 2 a role, 3 a name, 0 the end) and the text in the next 32, up to its first zero. No checksum. *Verified*
// with the lab's: the first 0 is in record 244, and no text fills the 32 bytes.
namespace Client::Tables
{
inline constexpr std::size_t CreditRecordSize = 33;
inline constexpr std::size_t CreditRecordCount = 400;
inline constexpr std::size_t CreditFileSize = CreditRecordSize * CreditRecordCount;

// The first byte's kinds, as 0x63ffe0 checks them.
enum class CreditKind : std::uint8_t
{
    End = 0,
    Title = 1,
    Role = 2,
    Name = 3,
};

struct CreditEntry
{
    // The raw byte: 0x63ffe0 compares with 0, 1, 2 and 3, and any other value is none.
    std::uint8_t kind = 0;
    std::string text;

    friend bool operator==(const CreditEntry&, const CreditEntry&) = default;
};

// FileTooShort below 13,200 bytes: fread reads what there is and the rest keeps what the window had, which MuRet does
// not have (the game's file has 13,200). The excess is ignored, like fread.
[[nodiscard]] Core::Expected<std::vector<CreditEntry>, Data::DataError> LoadCredits(std::span<const std::byte> file);

// As 0x63ffe0 reads it: after the last, End. In the original these are the window bytes after the table (+0x39a0, the
// index itself), which the game's file does not reach.
[[nodiscard]] std::uint8_t CreditKindAt(std::span<const CreditEntry> credits, std::size_t index);
} // namespace Client::Tables
