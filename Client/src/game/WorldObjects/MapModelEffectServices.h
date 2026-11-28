#pragma once

#include "WorldObjects/IWorldObjectCreationServices.h"

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Effects
{
class ModelEffects;
}
namespace Client::WorldObjects
{
// 0x606660's synchronous creation keeps the Noria effects in the same pool as the arrival.
class MapModelEffectServices final : public WorldObjects::IWorldObjectCreationServices
{
public:
    MapModelEffectServices(Effects::ModelEffects& effects, Crt::CrtRandom& random);
    bool CreateModelEffect(const WorldObjects::WorldObjectModelEffectRequest& request,
                           WorldObjects::WorldObject& source) noexcept override;

private:
    Effects::ModelEffects& m_effects;
    Crt::CrtRandom& m_random;
};
} // namespace Client::WorldObjects
