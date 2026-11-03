#include "Data/Tables/GateTable.h"

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <span>

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
