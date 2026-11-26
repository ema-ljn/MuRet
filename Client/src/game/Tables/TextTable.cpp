#include "Tables/TextTable.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Tables
{
TextTable::TextTable(std::vector<char> decoded) : m_records(std::move(decoded)) {}

// Checks the records it has, not TextCount: a moved-from table may have none.
std::optional<std::string_view> TextTable::Text(std::size_t index) const&
{
    if (index >= m_records.size() / TextRecordSize)
    {
        return std::nullopt;
    }
    const std::string_view record(m_records.data() + index * TextRecordSize, TextRecordSize);
    return record.substr(0, record.find('\0'));
}

Core::Expected<TextTable, Data::DataError> LoadTextTable(std::span<const std::byte> file)
{
    if (file.size() < TextTableSize)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    // Stored as char to give std::string_view without casts; decrypted as bytes.
    std::vector<char> records(TextTableSize);
    const std::span<std::byte> decoded = std::as_writable_bytes(std::span(records));
    std::ranges::copy(file.first(TextTableSize), decoded.begin());
    Crypto::Xor3(decoded);
    return TextTable(std::move(records));
}
} // namespace Client::Tables
