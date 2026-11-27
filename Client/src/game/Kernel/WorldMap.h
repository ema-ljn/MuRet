#pragma once

#include "Data/Terrain/TerrainObjects.h"
#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"

#include <cstdint>
#include <memory>
#include <vector>

// What 0x61a560 leaves of the loaded map on the CPU, in the original's globals: the grass shift, the objects and the
// effects with a model. The terrain is Terrain::TerrainState::current, and the GPU part (the textures and the models)
// is each scene's. Contract: Client/docs/research/06-render-escenas-y-ui.md, "La carga del mapa".
namespace Client::Kernel
{
struct WorldMap
{
    // Internal invalidation per LoadMap attempt; neither a counter nor a native rand.
    std::uint64_t loadRevision = 0;
    // 0x723c48: replaced only on reaching 0x4377a0, after the files.
    std::unique_ptr<World::KalimaWorld> kalima;
    // 0x7b0cca4: the 65,536 (rand() % 4) · 0.25 that 0x5f8300 fills (Q-C146).
    std::vector<float> grassOffsets;
    // EncTerrain<N>.obj, in the file's order, and what 0x606660 created with them and with the map's specials
    // (0x616ec0).
    std::vector<Data::Terrain::TerrainObject> objectRecords;
    std::vector<World::WorldObject> objects;
    World::WorldObjectRegistrations registrations;
    // The effects with a model (0x7491fd8), the arrival effect's (0x61f) included.
    World::ModelEffects effects;
};
} // namespace Client::Kernel
