#include "Scenes/LoadedWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/WorldLoadIdentity.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
LoadedWorld::LoadedWorld(Render::ITextureStore& store) : m_tiles(store) {}

Core::Expected<void, std::string> LoadedWorld::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                    std::vector<std::string>& skipped)
{
    Release();
    // Proyección local byte estable; las lecturas globales y los modelos de 0x61a560 no van acá.
    const auto effectiveMap = static_cast<std::uint8_t>(Data::MapAtWorldLoadStart(map, map));
    auto terrain = World::LoadWorldTerrain(dataRoot, effectiveMap);
    if (!terrain.HasValue())
    {
        return Core::Unexpected(terrain.Error());
    }
    if (auto tiles = m_tiles.Load(dataRoot, effectiveMap, skipped); !tiles.HasValue())
    {
        return tiles;
    }
    m_terrain = std::move(terrain).Value();
    World::TerrainView view;
    view.tiles = m_tiles.Sizes();
    view.mapping = m_terrain->mapping.get();
    view.heights = m_terrain->heights.get();
    view.attributes = m_terrain->attributes.get();
    view.lighting = m_terrain->lighting.get();
    view.map = effectiveMap;
    m_view = view;
    m_water = World::TerrainWater{};
    return {};
}

void LoadedWorld::Release()
{
    m_tiles.Release();
    m_terrain.reset();
    m_view = World::TerrainView{};
    m_water = World::TerrainWater{};
}

std::optional<std::uint8_t> LoadedWorld::LoadedMap() const
{
    return m_terrain ? std::optional<std::uint8_t>{m_view.map} : std::nullopt;
}

bool LoadedWorld::IsLoaded() const
{
    return m_terrain.has_value();
}

const World::TerrainLighting* LoadedWorld::Lighting() const
{
    return m_terrain ? m_terrain->lighting.get() : nullptr;
}

const World::WorldTerrain* LoadedWorld::Terrain() const
{
    return m_terrain ? &*m_terrain : nullptr;
}

World::WorldTerrain* LoadedWorld::Terrain()
{
    return m_terrain ? &*m_terrain : nullptr;
}

std::optional<World::WorldTerrain> LoadedWorld::TakeTerrain()
{
    m_view = World::TerrainView{};
    m_water = World::TerrainWater{};
    return std::exchange(m_terrain, std::nullopt);
}

void LoadedWorld::Step(Core::Duration clock, const World::TerrainRange& range)
{
    if (m_terrain)
    {
        World::UpdateWaves(m_water, clock, range, m_view.map);
    }
}

void LoadedWorld::DrawTerrain(Render::ModelBatch& batch, Core::Duration frameClock, const World::TerrainRange& range,
                              const World::TerrainPolygon& polygon, float fixedHeight,
                              const World::TerrainLighting* dynamicLighting)
{
    if (!m_terrain)
    {
        return;
    }
    World::TerrainView view = m_view;
    view.fixedHeight = fixedHeight;
    if (dynamicLighting != nullptr)
    {
        view.lighting = dynamicLighting;
    }
    m_water.offset = World::WaterOffsetAt(frameClock, m_view.map);
    World::DrawTerrain(batch, view, range, polygon, m_water);
}
} // namespace Client::Scenes
