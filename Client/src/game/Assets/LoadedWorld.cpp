#include "Assets/LoadedWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Maps/WorldLoadIdentity.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Client::Assets
{
LoadedWorld::LoadedWorld(Render::ITextureStore& store) : m_tiles(store) {}

Core::Expected<void, std::string> LoadedWorld::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                    std::vector<std::string>& skipped)
{
    Release();
    // A local byte-stable projection; the global reads and the models of 0x61a560 do not go here.
    const auto effectiveMap = static_cast<std::uint8_t>(Maps::MapAtWorldLoadStart(map, map));
    auto terrain = Terrain::LoadWorldTerrain(dataRoot, effectiveMap);
    if (!terrain.HasValue())
    {
        return Core::Unexpected(terrain.Error());
    }
    if (auto tiles = m_tiles.Load(dataRoot, effectiveMap, skipped); !tiles.HasValue())
    {
        return tiles;
    }
    m_terrain = std::move(terrain).Value();
    Terrain::TerrainView view;
    view.tiles = m_tiles.Sizes();
    view.mapping = m_terrain->mapping.get();
    view.heights = m_terrain->heights.get();
    view.attributes = m_terrain->attributes.get();
    view.lighting = m_terrain->lighting.get();
    view.map = effectiveMap;
    m_view = view;
    m_water = Terrain::TerrainWater{};
    return {};
}

void LoadedWorld::Release()
{
    m_tiles.Release();
    m_terrain.reset();
    m_view = Terrain::TerrainView{};
    m_water = Terrain::TerrainWater{};
}

std::optional<std::uint8_t> LoadedWorld::LoadedMap() const
{
    return m_terrain ? std::optional<std::uint8_t>{m_view.map} : std::nullopt;
}

bool LoadedWorld::IsLoaded() const
{
    return m_terrain.has_value();
}

const Terrain::TerrainLighting* LoadedWorld::Lighting() const
{
    return m_terrain ? m_terrain->lighting.get() : nullptr;
}

const Terrain::WorldTerrain* LoadedWorld::Terrain() const
{
    return m_terrain ? &*m_terrain : nullptr;
}

Terrain::WorldTerrain* LoadedWorld::Terrain()
{
    return m_terrain ? &*m_terrain : nullptr;
}

std::optional<Terrain::WorldTerrain> LoadedWorld::TakeTerrain()
{
    m_view = Terrain::TerrainView{};
    m_water = Terrain::TerrainWater{};
    return std::exchange(m_terrain, std::nullopt);
}

void LoadedWorld::Step(Core::Duration clock, const Terrain::TerrainRange& range)
{
    if (m_terrain)
    {
        Terrain::UpdateWaves(m_water, clock, range, m_view.map);
    }
}

void LoadedWorld::DrawTerrain(Render::ModelBatch& batch, Core::Duration frameClock, const Terrain::TerrainRange& range,
                              const Terrain::TerrainPolygon& polygon, float fixedHeight,
                              const Terrain::TerrainLighting* dynamicLighting)
{
    if (!m_terrain)
    {
        return;
    }
    Terrain::TerrainView view = m_view;
    view.fixedHeight = fixedHeight;
    if (dynamicLighting != nullptr)
    {
        view.lighting = dynamicLighting;
    }
    m_water.offset = Terrain::WaterOffsetAt(frameClock, m_view.map);
    Terrain::DrawTerrain(batch, view, range, polygon, m_water);
}
} // namespace Client::Assets
