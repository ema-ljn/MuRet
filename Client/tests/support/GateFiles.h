#pragma once

#include "Data/Tables/GateRecord.h"
#include "Data/Tables/GateTable.h"
#include "support/TableFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

// Archivos de gates sintéticos: usa el XOR3 de TableFiles, sin tocar fixtures del original.
namespace Client::Tests
{
inline std::vector<std::byte>
GateTableBytes(std::initializer_list<std::pair<std::size_t, Data::Tables::GateRecord>> records)
{
    std::vector<std::byte> plain(Data::Tables::GateTableSize);
    for (const auto& [index, gate] : records)
    {
        const std::array<std::uint8_t, Data::Tables::GateRecordSize> fields{
            gate.flag, gate.map, gate.x1, gate.y1, gate.x2, gate.y2, gate.target, gate.direction, gate.level};
        for (std::size_t field = 0; field < fields.size(); ++field)
        {
            plain.at(index * Data::Tables::GateRecordSize + field) = static_cast<std::byte>(fields[field]);
        }
    }
    return EncryptRecords(std::move(plain), Data::Tables::GateRecordSize);
}
} // namespace Client::Tests
