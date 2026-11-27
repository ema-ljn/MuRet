#pragma once

#include "Core/Expected.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Terrain/TerrainCells.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

// A map's terrain textures on the GPU: the 14 tiles of World<N>\ at 0x23..0x30, grass at 0x32..0x34, weather at
// 0x64..0x68 and slot 0x122 (Terrain/WorldTerrain.h), with their sizes, which the terrain reads for the UVs. The CPU
// side (the .map, the .att, the heights and the light) belongs to whoever holds the terrain: a scene (LoadedWorld) or
// the game's loaded map (WorldMap).
namespace Client::Assets
{
class ModelTextureTable;
// Whether Load also uploads the grass (0x32..0x34, Terrain::WorldGrassTextures): 0x61a560 uploads it in every scene,
// and in MuRet only the game draws it.
enum class GrassTextures : std::uint8_t
{
    Skip,
    Upload,
};

class TerrainTiles
{
public:
    // `store` must outlive this.
    explicit TerrainTiles(Render::ITextureStore& store);
    // Physical record shared with WorldModels; it must outlive TerrainTiles. A new table first releases the previous
    // slots; binding the same one again does not touch them.
    void BindTextureTable(ModelTextureTable& table);
    // Owns the slots it uploaded: a copy would release them twice.
    TerrainTiles(const TerrainTiles&) = delete;
    TerrainTiles& operator=(const TerrainTiles&) = delete;
    TerrainTiles(TerrainTiles&&) = delete;
    TerrainTiles& operator=(TerrainTiles&&) = delete;
    ~TerrainTiles() = default;

    // Releases the previous ones and uploads the tiles of `map` (the folder number), with `grass` the grass after them,
    // the weather at 0x64..0x68 and slot 0x122. Kalima loads Object25\water1/2 at slots 35/36 (water1 optional, water2
    // required) instead of tiles, grass and weather. An optional file goes to `skipped`, with what happened and the
    // path as the original asks for it (Q-C14); one that warns (TileGrass01, 0x122) and is missing or unreadable, or a
    // table that does not upload, is an error, and nothing stays uploaded.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         std::vector<std::string>& skipped,
                                                         GrassTextures grass = GrassTextures::Skip);

    // Releases the slots it uploaded. Without a Load, or a second time, it does nothing.
    void Release();

    // Empty without a load, after a failure or after a Release.
    [[nodiscard]] std::optional<std::uint8_t> LoadedMap() const;

    // The buffer size of each tile (TerrainView::tiles); 1 × 1 for those not uploaded.
    [[nodiscard]] const std::array<Terrain::TileSize, 256>& Sizes() const;

    // The buffer height of each uploaded grass, 0x32 to 0x34 (Terrain::TerrainGrass::heights); no value for those not
    // uploaded.
    [[nodiscard]] const std::array<std::optional<float>, Terrain::GrassTextureCount>& GrassHeights() const;

private:
    [[nodiscard]] Core::Expected<void, std::string>
    UploadTextures(std::span<const Data::Textures::LoadedTexture> textures);
    void RememberSlot(Render::TextureSlot slot);

    Render::ITextureStore& m_store;
    ModelTextureTable* m_sharedTextures = nullptr;
    std::optional<std::uint8_t> m_map;
    std::array<Terrain::TileSize, 256> m_sizes{};
    std::array<std::optional<float>, Terrain::GrassTextureCount> m_grass{};
    std::vector<Render::TextureSlot> m_uploaded;
};
} // namespace Client::Assets
