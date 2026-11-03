#include "Data/Tables/SkillTable.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Data::Tables
{
Core::Expected<SkillTable, DataError> LoadSkillTable(std::span<const std::byte> file)
{
    const std::optional<std::uint32_t> stored = StoredChecksum(file, SkillTableSize);
    if (!stored)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(SkillTableSize);
    if (TableChecksum(records, SkillChecksumKey, ChecksumParity::XorEvenDwords) != *stored)
    {
        return Core::Unexpected(DataError::ChecksumMismatch);
    }
    return SkillTable(records);
}
} // namespace Client::Data::Tables
