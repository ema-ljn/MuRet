#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Assets/TerrainTiles.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A scene's world while the scene lives (CL7.1): what 0x61a560 loads for the terrain (the .map, the .att, the heights,
// the light and the tiles, Terrain/WorldTerrain.h) and what the scene does with it on each step (0x5fbf60's waves) and
// each frame (0x5fc260(0)'s terrain pass).
namespace Client::Kernel
{
class LoadedWorld
{
public:
    // `store` must outlive this.
    explicit LoadedWorld(Render::ITextureStore& store);
    // It owns the slots it uploaded: a copy would release them twice.
    LoadedWorld(const LoadedWorld&) = delete;
    LoadedWorld& operator=(const LoadedWorld&) = delete;
    LoadedWorld(LoadedWorld&&) = delete;
    LoadedWorld& operator=(LoadedWorld&&) = delete;
    ~LoadedWorld() = default;

    // Normalizes `map` with argument = local observation (32 → 9) before it reads the terrain and the tiles; it does
    // not reproduce the mutable global reads or the models of 0x61a560. Reads the effective map's terrain and uploads
    // its tiles and slot 0x122. A tile that does not warn and is missing or unreadable is skipped and goes to
    // `skipped`, with what happened and the path as the original asks for it; one that warns (TileGrass01, the 0x122)
    // and is missing or unreadable, an unreadable terrain or a table that does not upload are an error, and nothing
    // stays loaded or uploaded.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         std::vector<std::string>& skipped);

    // Releases the slots it uploaded and the terrain. Without a Load, or a second time, it does nothing.
    void Release();

    [[nodiscard]] bool IsLoaded() const;

    // The effective map of the terrain it owns: empty before a load and after a failure, Release or TakeTerrain.
    [[nodiscard]] std::optional<std::uint8_t> LoadedMap() const;

    // The shaded base, if there is terrain. The scene's readers use WorldState's dynamic copy.
    [[nodiscard]] const Terrain::TerrainLighting* Lighting() const;
    // The whole terrain (the heights and the attributes 0x5f93c0 asks for), if any.
    [[nodiscard]] const Terrain::WorldTerrain* Terrain() const;
    [[nodiscard]] Terrain::WorldTerrain* Terrain();

    // Hands only the CPU part to the state that outlives the scene. It invalidates the terrain views; the GPU slots
    // stay this object's and Release frees them once.
    [[nodiscard]] std::optional<Terrain::WorldTerrain> TakeTerrain();

    // Step and DrawTerrain use LoadedMap()'s effective map (32 → 9), not the requested one.
    // 0x5fbf60, on each step: the wave of the range window's cells, with the step clock.
    void Step(Core::Duration clock, const Terrain::TerrainRange& range);

    // 0x5fc260(0): the water shift with the frame clock and the terrain pass over the range, with the trapezoid clip
    // and, if lent, the dynamic light. Without terrain, nothing.
    void DrawTerrain(Render::ModelBatch& batch, Core::Duration frameClock, const Terrain::TerrainRange& range,
                     const Terrain::TerrainPolygon& polygon, float fixedHeight = Terrain::FixedCornerHeight,
                     const Terrain::TerrainLighting* dynamicLighting = nullptr);

private:
    Assets::TerrainTiles m_tiles;
    std::optional<Terrain::WorldTerrain> m_terrain;
    Terrain::TerrainView m_view;
    Terrain::TerrainWater m_water;
};
} // namespace Client::Kernel
