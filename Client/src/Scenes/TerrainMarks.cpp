#include "Scenes/TerrainMarks.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
Core::Expected<void, std::string> MarkTheTerrainCell(Data::Terrain::TerrainAttributes* attributes, std::int32_t x,
                                                     std::int32_t y, std::uint8_t attribute, bool add,
                                                     std::string_view where)
{
    if (attributes == nullptr)
        return Core::Unexpected(std::format("{}: el atributo de ({}, {}) va al .att del mapa (0x7d0d348), que no "
                                            "está cargado",
                                            where, x, y));
    const std::int64_t index = (std::int64_t{y} << 8) + x;
    if (index < 0 || index >= static_cast<std::int64_t>(Data::Terrain::TerrainCellCount))
        return Core::Unexpected(std::format("{}: ({}, {}) cae fuera de los 0x10000 WORD de 0x7d0d348", where, x, y));
    std::uint16_t& cell = attributes->cells[static_cast<std::size_t>(index)];
    if (add)
        cell = static_cast<std::uint16_t>(cell | attribute);
    else
        cell = static_cast<std::uint16_t>(cell & ~std::uint16_t{attribute});
    return {};
}

Core::Expected<void, std::string> MarkTheTerrainArea(Data::Terrain::TerrainAttributes* attributes,
                                                     const TerrainArea& area, std::uint8_t attribute, bool add,
                                                     std::string_view where)
{
    for (std::int32_t row = 0; row < area.height; ++row)
    {
        for (std::int32_t column = 0; column < area.width; ++column)
        {
            if (auto marked = MarkTheTerrainCell(attributes, area.x + column, area.y + row, attribute, add, where);
                !marked.HasValue())
                return marked;
        }
    }
    return {};
}
} // namespace Client::Scenes
