#include "Terrain/SiegeTerrain.h"
#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Math/Ftol.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include <cmath>
#include <cstdint>
#include <string>
namespace Client::Terrain
{
Core::Expected<void, std::string> DeformTerrainHeights(Data::Terrain::TerrainHeights& heights, float x, float y,
                                                       float change, std::int32_t radius)
{
    x = static_cast<float>(static_cast<double>(x) * static_cast<double>(.01f));
    y = static_cast<float>(static_cast<double>(y) * static_cast<double>(.01f));
    const auto cx = Math::OriginalFtolLow(x), cy = Math::OriginalFtolLow(y);
    if (!cx || !cy)
        return Core::Unexpected(std::string("5f9580: centrofuera de_ftol"));
    if (radius <= 0)
        return {};
    const double radial = static_cast<float>(radius);
    for (int row = *cy - radius; row <= *cy + radius; ++row)
    {
        const double dy = static_cast<double>(y) - row;
        for (int col = *cx - radius; col <= *cx + radius; ++col)
        {
            const double dx = static_cast<double>(x) - col;
            const double weight = (radial - std::sqrt(dx * dx + dy * dy)) / radial;
            if (!(weight > 0))
                continue;
            auto& height =
                heights.cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(col), static_cast<std::uint8_t>(row))];
            height = static_cast<float>(weight * static_cast<double>(change) + height);
        }
    }
    return {};
}
Core::Expected<void, std::string> DeformTheSiegeTerrain(WorldTerrain& terrain, float x, float y, std::int32_t tileX,
                                                        std::int32_t tileY)
{
    if (!terrain.heights || !terrain.lighting || !terrain.rawLight)
        return Core::Unexpected(std::string("5f9580: faltanalturas o luzRGBcruda paraeldeformado"));
    if (auto deformed = DeformTerrainHeights(*terrain.heights, x, y, -30, 2); !deformed.HasValue())
        return deformed;
    ReshadeTerrainLightWindow(*terrain.lighting, *terrain.rawLight, *terrain.heights, tileX, tileY);
    return {};
}
} // namespace Client::Terrain
