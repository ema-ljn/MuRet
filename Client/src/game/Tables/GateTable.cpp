#include "Tables/GateTable.h"

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <span>

namespace Client::Tables
{
Core::Expected<GateTable, Data::DataError> LoadGateTable(std::span<const std::byte> file)
{
    if (file.size() < GateTableSize)
    {
        return Core::Unexpected(Data::DataError::FileTooShort);
    }
    return GateTable(file.first(GateTableSize));
}
} // namespace Client::Tables
