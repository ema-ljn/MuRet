#include "Data/Tables/GateRecord.h"

#include "Data/Tables/GateTable.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Data::Tables
{
std::optional<GateRecord> ReadGateRecord(const GateTable& table, std::size_t index)
{
    const auto record = table.Record(index);
    if (!record)
    {
        return std::nullopt;
    }
    return GateRecord{std::to_integer<std::uint8_t>((*record)[0]), std::to_integer<std::uint8_t>((*record)[1]),
                      std::to_integer<std::uint8_t>((*record)[2]), std::to_integer<std::uint8_t>((*record)[3]),
                      std::to_integer<std::uint8_t>((*record)[4]), std::to_integer<std::uint8_t>((*record)[5]),
                      std::to_integer<std::uint8_t>((*record)[6]), std::to_integer<std::uint8_t>((*record)[7]),
                      std::to_integer<std::uint8_t>((*record)[8])};
}
} // namespace Client::Data::Tables
