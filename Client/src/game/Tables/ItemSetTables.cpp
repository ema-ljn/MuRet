#include "Tables/ItemSetTables.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// 0x40c660 y 0x40c850, cotejados con el PE c32bbd5f: 05, "Las tablas de los conjuntos".
namespace Client::Tables
{
namespace
{
// Los registros de `file` si los sigue su checksum, o el error.
Core::Expected<std::span<const std::byte>, Data::DataError> CheckedRecords(std::span<const std::byte> file,
                                                                           std::size_t recordsSize, std::uint32_t key)
{
    const std::optional<std::uint32_t> stored = Data::Tables::StoredChecksum(file, recordsSize);
    if (!stored)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(recordsSize);
    if (Data::Tables::TableChecksum(records, key, Data::Tables::ChecksumParity::XorOddDwords) != *stored)
    {
        return Core::Unexpected(Data::DataError::ChecksumMismatch);
    }
    return records;
}
} // namespace

Core::Expected<ItemSetTypeTable, Data::DataError> LoadItemSetTypeTable(std::span<const std::byte> file)
{
    const auto records = CheckedRecords(file, ItemSetTypeTableSize, ItemSetTypeChecksumKey);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    return ItemSetTypeTable(records.Value());
}

Core::Expected<ItemSetOptionTable, Data::DataError> LoadItemSetOptionTable(std::span<const std::byte> file)
{
    const auto records = CheckedRecords(file, ItemSetOptionTableSize, ItemSetOptionChecksumKey);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    return ItemSetOptionTable(records.Value());
}
} // namespace Client::Tables
