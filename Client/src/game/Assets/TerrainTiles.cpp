#include "Assets/TerrainTiles.h"

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Maps/MapId.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Assets/ModelTextureTable.h"
#include "Terrain/TerrainCells.h"
#include "World/KalimaResources.h"
#include "Terrain/WorldTerrain.h"

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Client::Assets
{
namespace
{
// 0x616ec0 antes de 0x61a560: gota, rama propia, cáusticas y light01; después tiles, pasto, clima y 0x122.
std::vector<Terrain::WorldTileTexture> WantedTextures(std::uint8_t map, GrassTextures grass)
{
    std::vector<Terrain::WorldTileTexture> wanted{Terrain::DropTexture()};
    if (Maps::IsKalimaMap(map))
    {
        // 6189e9: el agua real vive enObject25, antes del salto de61a92a sobre los tiles ordinarios.
        for (const auto& texture : World::KalimaTextureSet())
        {
            // 6189e9 sexto0: water1 no avisa; 618a03 sexto1: water2 es requerido.
            constexpr std::uint16_t OptionalAlphaWaterSlot = 0x23;
            wanted.push_back(
                {std::string(texture.path), texture.slot, texture.sampling, texture.slot != OptionalAlphaWaterSlot});
        }
    }
    const auto tarkan = Terrain::TarkanObjectTextures(map);
    wanted.insert(wanted.end(), tarkan.begin(), tarkan.end());
    const auto icarus = Terrain::IcarusObjectTextures(map);
    wanted.insert(wanted.end(), icarus.begin(), icarus.end());
    const auto castle = Terrain::CastleSiegeTextures(map);
    wanted.insert(wanted.end(), castle.begin(), castle.end());
    const auto caustics = Terrain::CausticTextures(map);
    wanted.insert(wanted.end(), caustics.begin(), caustics.end());
    wanted.push_back(Terrain::ObjectLightTexture());
    if (!Maps::IsKalimaMap(map))
    {
        const auto tiles = Terrain::WorldTileTextures(map);
        wanted.insert(wanted.end(), tiles.begin(), tiles.end());
    }
    if (grass == GrassTextures::Upload)
    {
        const std::vector<Terrain::WorldTileTexture> grassTextures = Terrain::WorldGrassTextures(map);
        wanted.insert(wanted.end(), grassTextures.begin(), grassTextures.end());
    }
    const auto weather = Terrain::WorldWeatherTextures(map);
    wanted.insert(wanted.end(), weather.begin(), weather.end());
    wanted.push_back(Terrain::AcceptBoxTexture());
    return wanted;
}

// El buffer de una textura leída: el alto de un pasto o el tamaño de un tile.
void RecordTheBuffer(std::uint16_t slot, const Data::Textures::Texture& texture,
                     std::array<Terrain::TileSize, 256>& sizes,
                     std::array<std::optional<float>, Terrain::GrassTextureCount>& grass)
{
    const auto height = static_cast<float>(texture.textureHeight);
    if (slot >= Terrain::FirstGrassSlot && slot < Terrain::FirstGrassSlot + grass.size())
    {
        grass[static_cast<std::size_t>(slot - Terrain::FirstGrassSlot)] = height;
        return;
    }
    // El tile i es el slot i + 0x23; el 0x122, el tile 255 (Q-C19). Las de los objetos de Tarkan (0x5c2,
    // 0x5c3 y 0x716) quedan fuera de los 256 y no son de ningún tile.
    if (slot < Terrain::FirstTileSlot || static_cast<std::size_t>(slot - Terrain::FirstTileSlot) >= sizes.size())
        return;
    sizes[static_cast<std::size_t>(slot - Terrain::FirstTileSlot)] = {static_cast<float>(texture.textureWidth), height};
}
} // namespace

TerrainTiles::TerrainTiles(Render::ITextureStore& store) : m_store(store) {}

void TerrainTiles::BindTextureTable(ModelTextureTable& table)
{
    if (m_sharedTextures == &table)
        return;
    Release();
    table.Bind(m_store);
    m_sharedTextures = &table;
}

void TerrainTiles::RememberSlot(Render::TextureSlot slot)
{
    if (std::ranges::find(m_uploaded, slot) == m_uploaded.end())
        m_uploaded.push_back(slot);
}

Core::Expected<void, std::string> TerrainTiles::UploadTextures(std::span<const Data::Textures::LoadedTexture> textures)
{
    if (m_sharedTextures == nullptr)
    {
        // El backend puede haber subido un prefijo antes de fallar: Release limpia sus candidatos.
        for (const auto& texture : textures)
            RememberSlot(Render::TextureSlot{texture.slot});
        return m_store.Load(textures);
    }
    for (const auto& texture : textures)
    {
        const auto uploaded = m_sharedTextures->Upload(texture);
        if (!uploaded.HasValue())
            return uploaded;
        RememberSlot(Render::TextureSlot{texture.slot});
    }
    return {};
}

Core::Expected<void, std::string> TerrainTiles::Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                     std::vector<std::string>& skipped, GrassTextures grass)
{
    Release();
    std::vector<Data::Textures::LoadedTexture> textures;
    std::array<Terrain::TileSize, 256> sizes{};
    std::array<std::optional<float>, Terrain::GrassTextureCount> grassHeights{};
    for (const Terrain::WorldTileTexture& tile : WantedTextures(map, grass))
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
    auto uploaded = UploadTextures(textures);
    if (!uploaded.HasValue())
    {
        Release();
        return Core::Unexpected(uploaded.Error());
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
        if (m_sharedTextures != nullptr)
            m_sharedTextures->Release(slot);
        else
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

const std::array<Terrain::TileSize, 256>& TerrainTiles::Sizes() const
{
    return m_sizes;
}

const std::array<std::optional<float>, Terrain::GrassTextureCount>& TerrainTiles::GrassHeights() const
{
    return m_grass;
}
} // namespace Client::Assets
