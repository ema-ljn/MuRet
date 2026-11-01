#include "Data/Tables/ItemTable.h"

#include "Data/Tables/TableChecksum.h"

#include <cstdint>
#include <optional>

namespace Client::Data::Tables
{

Core::Expected<LoadedItemTable, DataError> LoadItemTable(std::span<const std::byte> file)
{
    if (file.size() < ItemTableSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    const std::span<const std::byte> records = file.first(ItemTableSize);
    const std::optional<std::uint32_t> stored = StoredChecksum(file, ItemTableSize);
    const bool matches =
        stored.has_value() && TableChecksum(records, ItemChecksumKey, ChecksumParity::XorOddDwords) == *stored;
    return LoadedItemTable{ItemTable(records), matches};
}

} // namespace Client::Data::Tables
