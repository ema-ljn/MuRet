#pragma once

#include "Core/Expected.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "World/TerrainCells.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Las texturas del terreno de un mapa en la GPU (CL7.1): los 14 tiles de World<N>\ en 0x23..0x30 y el
// slot 0x122 que sube 0x61a560 (World/WorldTerrain.h), con el ancho y el alto del buffer de cada uno,
// que el terreno lee para las UV. Lo de la CPU (el .map, el .att, las alturas y la luz) es de quien
// tenga el terreno: una escena (LoadedWorld) o el mapa cargado del juego (WorldMap).
namespace Client::Scenes
{
class TerrainTiles
{
public:
    // `store` tiene que vivir más que esto.
    explicit TerrainTiles(Render::ITextureStore& store);
    // Es dueño de los slots que subió: una copia los soltaría dos veces.
    TerrainTiles(const TerrainTiles&) = delete;
    TerrainTiles& operator=(const TerrainTiles&) = delete;
    TerrainTiles(TerrainTiles&&) = delete;
    TerrainTiles& operator=(TerrainTiles&&) = delete;
    ~TerrainTiles() = default;

    // Suelta los de antes y sube los tiles de `map` (el número de la carpeta) y el slot 0x122. Un tile
    // que no avisa y falta o no se lee se saltea y va a `skipped`, con lo que pasó y la ruta como la pide
    // el original (Q-C14); uno que avisa (TileGrass01, el 0x122) y falta o no se lee, o la tabla que no
    // sube, son un error, y no queda nada subido.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         std::vector<std::string>& skipped);

    // Suelta los slots que subió. Sin un Load, o una segunda vez, no hace nada.
    void Release();

    // El mapa de los tiles subidos; vacío sin carga, tras un fallo o un Release.
    [[nodiscard]] std::optional<std::uint8_t> LoadedMap() const;

    // El tamaño del buffer de cada tile (TerrainView::tiles); en 1 × 1 los que no se subieron.
    [[nodiscard]] const std::array<World::TileSize, 256>& Sizes() const;

private:
    Render::ITextureStore& m_store;
    std::optional<std::uint8_t> m_map;
    std::array<World::TileSize, 256> m_sizes{};
    std::vector<Render::TextureSlot> m_uploaded;
};
} // namespace Client::Scenes
