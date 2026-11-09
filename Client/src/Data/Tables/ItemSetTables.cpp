#include "Data/Tables/ItemSetTables.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// 0x40c660 y 0x40c850, cotejados con el PE c32bbd5f: 05, "Las tablas de los conjuntos".
namespace Client::Data::Tables
{
namespace
{
// Los registros de `file` si los sigue su checksum, o el error.
Core::Expected<std::span<const std::byte>, DataError> CheckedRecords(std::span<const std::byte> file,
                                                                     std::size_t recordsSize, std::uint32_t key)
{
    const std::optional<std::uint32_t> stored = StoredChecksum(file, recordsSize);
    if (!stored)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(recordsSize);
    if (TableChecksum(records, key, ChecksumParity::XorOddDwords) != *stored)
    {
        return Core::Unexpected(DataError::ChecksumMismatch);
    }
    return records;
}
} // namespace

Core::Expected<ItemSetTypeTable, DataError> LoadItemSetTypeTable(std::span<const std::byte> file)
{
    const auto records = CheckedRecords(file, ItemSetTypeTableSize, ItemSetTypeChecksumKey);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    return ItemSetTypeTable(records.Value());
}

Core::Expected<ItemSetOptionTable, DataError> LoadItemSetOptionTable(std::span<const std::byte> file)
{
    const auto records = CheckedRecords(file, ItemSetOptionTableSize, ItemSetOptionChecksumKey);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    return ItemSetOptionTable(records.Value());
}
} // namespace Client::Data::Tables
