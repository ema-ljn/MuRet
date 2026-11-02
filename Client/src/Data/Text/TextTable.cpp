#include "Data/Text/TextTable.h"

#include "Core/Xor3.h"

#include <algorithm>
#include <utility>

namespace Client::Data::Text
{
TextTable::TextTable(std::vector<char> decoded) : m_records(std::move(decoded)) {}

// Mira los registros que tiene y no TextCount: una tabla movida puede no tener ninguno.
std::optional<std::string_view> TextTable::Text(std::size_t index) const&
{
    if (index >= m_records.size() / TextRecordSize)
    {
        return std::nullopt;
    }
    const std::string_view record(m_records.data() + index * TextRecordSize, TextRecordSize);
    return record.substr(0, record.find('\0'));
}

Core::Expected<TextTable, DataError> LoadTextTable(std::span<const std::byte> file)
{
    if (file.size() < TextTableSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    // Los textos se guardan como char para dar std::string_view sin casts; se descifran vistos
    // como bytes.
    std::vector<char> records(TextTableSize);
    const std::span<std::byte> decoded = std::as_writable_bytes(std::span(records));
    std::ranges::copy(file.first(TextTableSize), decoded.begin());
    Core::Xor3(decoded);
    return TextTable(std::move(records));
}
} // namespace Client::Data::Text
