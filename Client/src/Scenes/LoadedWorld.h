#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Scenes/TerrainTiles.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// El mundo de una escena mientras la escena vive (CL7.1): lo que 0x61a560 carga para el terreno (el
// .map, el .att, las alturas, la luz y los tiles, World/WorldTerrain.h) y lo que la escena hace con él
// en cada paso (las olas de 0x5fbf60) y en cada frame (el pase del terreno de 0x5fc260(0)).
namespace Client::Scenes
{
class LoadedWorld
{
public:
    // `store` tiene que vivir más que esto.
    explicit LoadedWorld(Render::ITextureStore& store);
    // Es dueño de los slots que subió: una copia los soltaría dos veces.
    LoadedWorld(const LoadedWorld&) = delete;
    LoadedWorld& operator=(const LoadedWorld&) = delete;
    LoadedWorld(LoadedWorld&&) = delete;
    LoadedWorld& operator=(LoadedWorld&&) = delete;
    ~LoadedWorld() = default;

    // Normaliza `map` con argumento=observación local (32 → 9) antes de leer terreno y tiles;
    // no reproduce las lecturas globales mutables ni los modelos de 0x61a560.
    // Lee el terreno del mapa efectivo y sube sus tiles y el slot 0x122. Un tile que no avisa y falta
    // o no se lee se saltea y va a `skipped`, con lo que pasó y la ruta como la pide el original; uno que avisa
    // (TileGrass01, el 0x122) y falta o no se lee, el terreno que no se lee o la tabla que no sube son
    // un error, y no queda nada cargado ni subido.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         std::vector<std::string>& skipped);

    // Suelta los slots que subió y el terreno. Sin un Load, o una segunda vez, no hace nada.
    void Release();

    [[nodiscard]] bool IsLoaded() const;

    // La identidad efectiva del terreno que posee: vacía sin carga, tras fallo, Release o TakeTerrain.
    [[nodiscard]] std::optional<std::uint8_t> LoadedMap() const;

    // La base sombreada, si hay terreno. Los lectores de escena usan la copia dinámica de WorldState.
    [[nodiscard]] const World::TerrainLighting* Lighting() const;
    // El terreno entero (las alturas y los atributos que pide 0x5f93c0), si hay.
    [[nodiscard]] const World::WorldTerrain* Terrain() const;
    [[nodiscard]] World::WorldTerrain* Terrain();

    // Transfiere sólo CPU al estado que sobrevive a la escena. Invalida las vistas del terreno;
    // los slots GPU siguen siendo de este objeto y Release los suelta una sola vez.
    [[nodiscard]] std::optional<World::WorldTerrain> TakeTerrain();

    // Step y DrawTerrain usan la identidad efectiva de LoadedMap() (32 → 9), no la solicitada.
    // 0x5fbf60, en cada paso: la ola de las celdas de la ventana del rango, con el reloj del paso.
    void Step(Core::Duration clock, const World::TerrainRange& range);

    // 0x5fc260(0): el corrimiento del agua con el reloj del frame y el pase del terreno en el rango,
    // con el recorte del trapecio y, si se presta, la luz dinámica. Sin terreno, nada.
    void DrawTerrain(Render::ModelBatch& batch, Core::Duration frameClock, const World::TerrainRange& range,
                     const World::TerrainPolygon& polygon, float fixedHeight = World::FixedCornerHeight,
                     const World::TerrainLighting* dynamicLighting = nullptr);

private:
    TerrainTiles m_tiles;
    std::optional<World::WorldTerrain> m_terrain;
    World::TerrainView m_view;
    World::TerrainWater m_water;
};
} // namespace Client::Scenes
