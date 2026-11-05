#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/ITextureStore.h"
#include "Render/ModelBatch.h"
#include "Render/TextureSlot.h"
#include "World/TerrainCells.h"
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

    // Lee el terreno de `map` y sube sus tiles y el slot 0x122. Un tile que no avisa y falta se
    // saltea y va a `skipped`, con la ruta como la pide el original; uno que avisa (TileGrass01, el
    // 0x122) y falta, el terreno que no se lee o la tabla que no sube son un error, y no queda nada
    // cargado ni subido.
    [[nodiscard]] Core::Expected<void, std::string> Load(const std::filesystem::path& dataRoot, std::uint8_t map,
                                                         std::vector<std::string>& skipped);

    // Suelta los slots que subió y el terreno. Sin un Load, o una segunda vez, no hace nada.
    void Release();

    [[nodiscard]] bool IsLoaded() const;

    // La luz sombreada del terreno (la de 0x5f99e0 para los modelos), si hay terreno.
    [[nodiscard]] const World::TerrainLighting* Lighting() const;

    // 0x5fbf60, en cada paso: la ola de las celdas de la ventana del rango, con el reloj del paso.
    void Step(Core::Duration clock, const World::TerrainRange& range);

    // 0x5fc260(0): el corrimiento del agua con el reloj del frame y el pase del terreno en el rango.
    // Sin terreno, nada.
    void DrawTerrain(Render::ModelBatch& batch, Core::Duration frameClock, const World::TerrainRange& range);

private:
    Render::ITextureStore& m_store;
    std::optional<World::WorldTerrain> m_terrain;
    World::TerrainView m_view;
    World::TerrainWater m_water;
    std::vector<Render::TextureSlot> m_uploaded;
};
} // namespace Client::Scenes
