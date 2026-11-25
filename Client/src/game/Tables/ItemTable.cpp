#include "Tables/ItemTable.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Tables
{
Core::Expected<LoadedItemTable, Data::DataError> LoadItemTable(std::span<const std::byte> file)
{
    if (file.size() < ItemTableSize)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(ItemTableSize);
    const std::optional<std::uint32_t> stored = Data::Tables::StoredChecksum(file, ItemTableSize);
    const bool matches =
        stored.has_value() && Data::Tables::TableChecksum(records, Data::Tables::ItemChecksumKey,
                                                          Data::Tables::ChecksumParity::XorOddDwords) == *stored;
    return LoadedItemTable{ItemTable(records), matches};
}
} // namespace Client::Tables
