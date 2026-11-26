#include "Tables/SkillTable.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Tables
{
Core::Expected<SkillTable, Data::DataError> LoadSkillTable(std::span<const std::byte> file)
{
    const std::optional<std::uint32_t> stored = Data::Tables::StoredChecksum(file, SkillTableSize);
    if (!stored)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(SkillTableSize);
    if (Data::Tables::TableChecksum(records, Data::Tables::SkillChecksumKey,
                                    Data::Tables::ChecksumParity::XorEvenDwords) != *stored)
    {
        return Core::Unexpected(Data::DataError::ChecksumMismatch);
    }
    return SkillTable(records);
}
} // namespace Client::Tables
