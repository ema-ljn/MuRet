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
    // 0x42c6b0: el objeto 79 del 30 pide el 0x135 con el objeto de dueño; con la tabla llena, 0x606660 sigue.
    if (request.type == World::GatePlaneEffect)
    {
        static_cast<void>(
            m_effects.CreateGatePlane(request.position, source.transform.angles, source.light, request.owner != 0));
        return true;
    }
    if (request.type < FirstTowerEffect || request.type > LastTowerEffect)
        return false;
    static_cast<void>(
        m_effects.CreateNoriaEffect(request.type, request.position, source.transform.angles, source.light, m_random));
    return true;
}
} // namespace Client::Scenes
