#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Text.bmd, the interface texts (05-formatos-de-datos.md, "Tablas .bmd"; in the original 0x5495e0's loader). 0x96000
// bytes: 2048 records of 300, no header, count or checksum, XOR3 end to end. Text i is at byte 300 * i and ends at the
// first '\0' or the record's end: in the real file record 7 fills its 300 bytes without '\0' (Q-C07). The original
// reads it as a C string and goes on into record 8 (519 bytes with the real file); MuRet cuts at the record's 300.
// Texts stay as bytes, one byte per letter: the text rasterizer (Render/Text) reads each byte as the Latin-1
// character of its number, without UTF-8 (Q-C67). The original uses some as sprintf formats (FUN_00633300); MuRet
// never passes a data text as a format.
namespace Client::Tables
{
// Text i is the one the original looks up with index i.
inline constexpr std::size_t TextCount = 2048;
// With its '\0' if it has one (7 does not, Q-C07).
inline constexpr std::size_t TextRecordSize = 300;
// The whole file: no header, count or checksum.
inline constexpr std::size_t TextTableSize = TextCount * TextRecordSize;
static_assert(TextTableSize == 0x96000, "el tamaño que mide el research (05, Tablas .bmd)");

class TextTable
{
public:
    // std::nullopt if index >= TextCount. An index from the server can overflow (FUN_004da070 adds 2020 to a packet
    // byte), and the original would read past the table. The text points into the table: valid until it is destroyed,
    // moved or assigned.
    std::optional<std::string_view> Text(std::size_t index) const&;

    // On a temporary table it does not compile (`LoadTextTable(file).Value().Text(0)`): the text would dangle. If the
    // temporary arrives through a `const TextTable&` the compiler no longer sees it, and the text dangles anyway.
    std::optional<std::string_view> Text(std::size_t index) const&& = delete;

private:
    explicit TextTable(std::vector<char> decoded);

    // Only LoadTextTable builds a table. A moved-from table is valid but unspecified (empty in practice), and its
    // Text() does not read outside what it kept.
    friend Core::Expected<TextTable, Data::DataError> LoadTextTable(std::span<const std::byte> file);

    std::vector<char> m_records;
};

// FileTooShort if `file` is shorter: the original would read anyway and leave whatever was in memory. A longer file's
// rest is ignored, like the original's fixed-size fread.
Core::Expected<TextTable, Data::DataError> LoadTextTable(std::span<const std::byte> file);

[[nodiscard]] inline std::string_view TextOr(const TextTable& texts, std::size_t index, std::string_view fallback = {})
{
    return texts.Text(index).value_or(fallback);
}

// A text of a temporary table would dangle, as with Text().
std::string_view TextOr(const TextTable&& texts, std::size_t index, std::string_view fallback = {}) = delete;
} // namespace Client::Tables
