#include "Scenes/LoadedWorld.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Textures/TextureSet.h"
#include "Data/WorldLoadIdentity.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/TextureSlot.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Client::Scenes
{
LoadedWorld::LoadedWorld(Render::ITextureStore& store) : m_store(store) {}

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
    std::vector<World::WorldTileTexture> wanted = World::WorldTileTextures(effectiveMap);
    wanted.push_back(World::AcceptBoxTexture());
    std::vector<Data::Textures::LoadedTexture> textures;
    World::TerrainView view;
    for (const World::WorldTileTexture& tile : wanted)
    {
        const std::array<Data::Textures::TextureSetEntry, 1> entry{{{tile.path, tile.slot, tile.sampling}}};
        auto read = Data::Textures::LoadTextureSet(dataRoot, entry);
        if (!read.HasValue())
        {
            // Un tile que no avisa, falte o no se lea, se saltea (Q-C14: 0x63b870 devuelve false y sigue).
            if (!tile.required)
            {
                skipped.push_back(Data::Textures::Describe(read.Error()));
                continue;
            }
            return Core::Unexpected(Data::Textures::Describe(read.Error()));
        }
        Data::Textures::LoadedTexture& texture = read.Value().front();
        // El tile i es el slot i + 0x23; el 0x122, el tile 255 (Q-C19).
        view.tiles[static_cast<std::size_t>(tile.slot - World::FirstTileSlot) % view.tiles.size()] = {
            static_cast<float>(texture.texture.textureWidth), static_cast<float>(texture.texture.textureHeight)};
        textures.push_back(std::move(texture));
    }
    auto uploaded = m_store.Load(textures);
    if (!uploaded.HasValue())
    {
        for (const Data::Textures::LoadedTexture& texture : textures)
        {
            m_store.Release(Render::TextureSlot{texture.slot});
        }
        return Core::Unexpected(uploaded.Error());
    }
    for (const Data::Textures::LoadedTexture& texture : textures)
    {
        m_uploaded.push_back(Render::TextureSlot{texture.slot});
    }
    m_terrain = std::move(terrain).Value();
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
    for (const Render::TextureSlot slot : m_uploaded)
    {
        m_store.Release(slot);
    }
    m_uploaded.clear();
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
                              const World::TerrainPolygon& polygon, float fixedHeight)
{
    if (!m_terrain)
    {
        return;
    }
    m_view.fixedHeight = fixedHeight;
    m_water.offset = World::WaterOffsetAt(frameClock, m_view.map);
    World::DrawTerrain(batch, m_view, range, polygon, m_water);
}
} // namespace Client::Scenes
