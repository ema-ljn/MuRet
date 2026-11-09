#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "World/ModelEffects.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <vector>

// Lo que 0x61a560 deja del mapa cargado en la CPU, en los globales del original: las tablas del
// terreno, el corrimiento del pasto, los objetos y los efectos con modelo. Lo de la GPU (las texturas
// y los modelos) es de cada escena. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La
// carga del mapa".
namespace Client::Scenes
{
struct WorldMap
{
    World::WorldTerrain terrain;
    // 0x7b0cca4: los 65.536 (rand() % 4) · 0,25 que llena 0x5f8300 (Q-C146).
    std::vector<float> grassOffsets;
    // EncTerrain<N>.obj, en el orden del archivo, y lo que 0x606660 creó con ellos y con los
    // especiales del mapa (0x616ec0).
    std::vector<Data::Terrain::TerrainObject> objectRecords;
    std::vector<World::WorldObject> objects;
    World::WorldObjectRegistrations registrations;
    // Los efectos con modelo (0x7491fd8), los del efecto de llegada (0x61f) incluidos.
    World::ModelEffects effects;
};
} // namespace Client::Scenes
