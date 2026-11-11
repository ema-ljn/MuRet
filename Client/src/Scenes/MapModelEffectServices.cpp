#include "Scenes/MapModelEffectServices.h"

#include "World/IWorldObjectCreationServices.h"
#include "World/ModelEffects.h"
#include "World/WorldObjects.h"

#include <cstdint>

namespace Client::Scenes
{
MapModelEffectServices::MapModelEffectServices(World::ModelEffects& effects, Render::Models::CrtRandom& random)
    : m_effects(effects), m_random(random)
{
}
bool MapModelEffectServices::CreateModelEffect(const World::WorldObjectModelEffectRequest& request,
                                               World::WorldObject& source) noexcept
{
    constexpr std::int16_t FirstTowerEffect = 322;
    constexpr std::int16_t LastTowerEffect = 324;
    if (request.type < FirstTowerEffect || request.type > LastTowerEffect)
        return false;
    static_cast<void>(
        m_effects.CreateNoriaEffect(request.type, request.position, source.transform.angles, source.light, m_random));
    return true;
}
} // namespace Client::Scenes
