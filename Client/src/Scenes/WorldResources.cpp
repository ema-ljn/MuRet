#include "Scenes/WorldResources.h"

#include "Core/Expected.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <bit>
#include <cstdint>

#include "Scenes/CreatePreview.h"
#include "Scenes/LoadedWorld.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/WorldModels.h"
#include "Scenes/WorldObjectRenderState.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

namespace Client::Scenes
{
WorldResources::WorldResources(Render::ITextureStore& textures) : terrain(textures) {}
WorldResources::~WorldResources()
{
    Release();
}

Core::Expected<void, World::TerrainPolygonError>
WorldResources::UpdateTerrainPolygon(const World::TerrainPolygonInput& input, const World::GameCamera& camera)
{
    const auto requested = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(camera.level));
    const auto frame = World::TerrainPolygonFrameOf(
        terrainPolygonState, input,
        {.requestedLevel = requested, .distance = camera.distance, .targetDistance = camera.targetDistance});
    if (!frame.HasValue())
        return Core::Unexpected(frame.Error());
    terrainPolygonState = frame.Value().nextState;
    terrainPolygon = frame.Value().polygon;
    return {};
}

void WorldResources::Release()
{
    // Evacua préstamos antes de destruir las bibliotecas; conserva bytes y pose históricos.
    objectRenderState.ReleaseModels();
    if (loginLogoTextures)
        loginLogoTextures->Release();
    loginLogoTextures.reset();
    loginLogoSlots.reset();
    loginLogo.Clear();
    selectionModels.Release();
    models.Release();
    terrain.Release();
    objectRecords.clear();
    objects.clear();
    effects.Clear();
    sprites.Clear();
    particles.Clear();
    leaves.Clear();
}
} // namespace Client::Scenes
