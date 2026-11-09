#pragma once

#include "Core/Expected.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/LoadedWorld.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/WorldModels.h"
#include "Scenes/WorldObjectRenderState.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingState.h"
#include "World/TerrainPickingTarget.h"
#include "World/TerrainPickingSegment.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"
#include "World/WorldObjectRegistrations.h"
#include "World/TerrainWalls.h"

#include <optional>
#include <vector>

namespace Client::Render
{
class ITextureStore;
}

namespace Client::World
{
struct GameCamera;
}

namespace Client::Scenes
{
// Un solo dueño de las cargas que comparten slots. Las escenas toman referencias: Leave de una
// escena vieja no puede liberar el mapa que el aplicador acaba de cargar en estos mismos objetos.
struct WorldResources
{
    explicit WorldResources(Render::ITextureStore& textures);
    WorldResources(const WorldResources&) = delete;
    WorldResources& operator=(const WorldResources&) = delete;
    ~WorldResources();
    // Antes de cargar otro mapa: suelta primero los consumidores de slots y después sus tablas.
    void Release();
    [[nodiscard]] Core::Expected<void, World::TerrainPolygonError>
    UpdateTerrainPolygon(const World::TerrainPolygonInput& input, const World::GameCamera& camera);

    // Un solo dueño de M+a0/flag88 y pose; los pases toman vistas sin copiar los bytes.
    WorldObjectRenderState objectRenderState;
    LoadedWorld terrain;
    WorldModels models;
    World::TerrainWalls walls;
    // 5fbb00 publica N/D; Release de mapa no es un reset de estos globales.
    World::ModelEffectPlanes modelEffectPlanes;

    World::TerrainRange terrainRange;
    World::TerrainPolygon terrainPolygon{.everything = true};
    World::TerrainPolygonState terrainPolygonState; // Nivel efectivo durable; Release no lo reinicia.
    World::TerrainPickingState terrainPicking;
    World::TerrainPickingTarget terrainTarget;
    std::optional<World::TerrainPickingSegment> pickingRay;
    World::WorldObjectRegistrations registrations;
    std::vector<Data::Terrain::TerrainObject> objectRecords;
    std::vector<World::WorldObject> objects;
    World::ModelEffects effects;
    World::FrameSprites sprites;
    World::Particles particles;
    World::Leaves leaves;
    SingleModelLibrary loginLogo;
    std::optional<Render::Models::ModelTextureSlots> loginLogoSlots;
    std::optional<SceneTextures> loginLogoTextures;
    SelectionModels selectionModels;
};
} // namespace Client::Scenes
