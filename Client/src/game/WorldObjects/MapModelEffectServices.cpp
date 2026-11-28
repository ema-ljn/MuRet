#include "WorldObjects/MapModelEffectServices.h"

#include "Crt/CrtRandom.h"
#include "WorldObjects/IWorldObjectCreationServices.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/WorldObjects.h"

#include <cstdint>

namespace Client::WorldObjects
{
MapModelEffectServices::MapModelEffectServices(Effects::ModelEffects& effects, Crt::CrtRandom& random)
    : m_effects(effects), m_random(random)
{
}
bool MapModelEffectServices::CreateModelEffect(const WorldObjects::WorldObjectModelEffectRequest& request,
                                               WorldObjects::WorldObject& source) noexcept
{
    constexpr std::int16_t FirstTowerEffect = 322;
    constexpr std::int16_t LastTowerEffect = 324;
    // 0x42c6b0: el objeto 79 del 30 pide el 0x135 con el objeto de dueño; con la tabla llena, 0x606660 sigue.
    if (request.type == Effects::GatePlaneEffect)
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
} // namespace Client::WorldObjects
