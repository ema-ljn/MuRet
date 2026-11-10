#include "Scenes/TerrainTiles.h"

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "World/TerrainCells.h"
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
namespace
{
// Lo que sube Load, en el orden de 0x61a560: los tiles, el pasto (0x61abb4..0x61ac36) y el 0x122.
std::vector<World::WorldTileTexture> WantedTextures(std::uint8_t map, GrassTextures grass)
{
    std::vector<World::WorldTileTexture> wanted = World::WorldTileTextures(map);
    if (grass == GrassTextures::Upload)
    {
        const std::vector<World::WorldTileTexture> grassTextures = World::WorldGrassTextures(map);
        wanted.insert(wanted.end(), grassTextures.begin(), grassTextures.end());
    }
    wanted.push_back(World::AcceptBoxTexture());
    return wanted;
}

// El buffer de una textura leída: el alto de un pasto o el tamaño de un tile.
void RecordTheBuffer(std::uint16_t slot, const Data::Textures::Texture& texture,
                     std::array<World::TileSize, 256>& sizes,
                     std::array<std::optional<float>, World::GrassTextureCount>& grass)
{
    const auto height = static_cast<float>(texture.textureHeight);
    if (slot >= World::FirstGrassSlot && slot < World::FirstGrassSlot + grass.size())
    {
        grass[static_cast<std::size_t>(slot - World::FirstGrassSlot)] = height;
        return;
    }
    // El tile i es el slot i + 0x23; el 0x122, el tile 255 (Q-C19).
    sizes[static_cast<std::size_t>(slot - World::FirstTileSlot) % sizes.size()] = {
        static_cast<float>(texture.textureWidth), height};
}
} // namespace

TerrainTiles::TerrainTiles(Render::ITextureStore& store) : m_store(store) {}

Core::Expected<void, std::string> TerrainTiles::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                     std::vector<std::string>& skipped, GrassTextures grass)
{
    Release();
    std::vector<Data::Textures::LoadedTexture> textures;
    std::array<World::TileSize, 256> sizes{};
    std::array<std::optional<float>, World::GrassTextureCount> grassHeights{};
    for (const World::WorldTileTexture& tile : WantedTextures(map, grass))
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
        RecordTheBuffer(tile.slot, texture.texture, sizes, grassHeights);
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
    m_sizes = sizes;
    m_grass = grassHeights;
    m_map = map;
    return {};
}

void TerrainTiles::Release()
{
    for (const Render::TextureSlot slot : m_uploaded)
    {
        m_store.Release(slot);
    }
    m_uploaded.clear();
    m_sizes = {};
    m_grass = {};
    m_map.reset();
}

std::optional<std::uint8_t> TerrainTiles::LoadedMap() const
{
    return m_map;
}

const std::array<World::TileSize, 256>& TerrainTiles::Sizes() const
{
    return m_sizes;
}

const std::array<std::optional<float>, World::GrassTextureCount>& TerrainTiles::GrassHeights() const
{
    return m_grass;
}
} // namespace Client::Scenes
