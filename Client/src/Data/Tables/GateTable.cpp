#include "Data/Tables/GateTable.h"

namespace Client::Data::Tables
{
Core::Expected<GateTable, DataError> LoadGateTable(std::span<const std::byte> file)
{
    if (file.size() < GateTableSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }
    return GateTable(file.first(GateTableSize));
}
} // namespace Client::Data::Tables
