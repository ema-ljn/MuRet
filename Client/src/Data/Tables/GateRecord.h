#pragma once

#include "Data/Tables/GateTable.h"

#include <cstddef>
#include <cstdint>
#include <optional>

// Los nueve campos de Gate.bmd (05 § Gate.bmd), copiados de la tabla ya descifrada.
namespace Client::Data::Tables
{
struct GateRecord
{
    std::uint8_t flag = 0;
    std::uint8_t map = 0;
    std::uint8_t x1 = 0;
    std::uint8_t y1 = 0;
    std::uint8_t x2 = 0;
    std::uint8_t y2 = 0;
    std::uint8_t target = 0;
    std::uint8_t direction = 0;
    std::uint8_t level = 0;

    friend bool operator==(const GateRecord&, const GateRecord&) = default;
};

// El índice es el número de gate. Fuera de la tabla, std::nullopt.
[[nodiscard]] std::optional<GateRecord> ReadGateRecord(const GateTable& table, std::size_t index);
} // namespace Client::Data::Tables
