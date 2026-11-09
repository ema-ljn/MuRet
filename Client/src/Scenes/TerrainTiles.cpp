#include "Scenes/TerrainTiles.h"

#include "Core/Expected.h"
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
TerrainTiles::TerrainTiles(Render::ITextureStore& store) : m_store(store) {}

Core::Expected<void, std::string> TerrainTiles::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                     std::vector<std::string>& skipped)
{
    Release();
    std::vector<World::WorldTileTexture> wanted = World::WorldTileTextures(map);
    wanted.push_back(World::AcceptBoxTexture());
    std::vector<Data::Textures::LoadedTexture> textures;
    std::array<World::TileSize, 256> sizes{};
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
        sizes[static_cast<std::size_t>(tile.slot - World::FirstTileSlot) % sizes.size()] = {
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
    m_sizes = sizes;
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
} // namespace Client::Scenes
