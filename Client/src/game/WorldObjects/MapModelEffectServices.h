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
// La creación síncrona de 0x606660 conserva los efectos de Noria en el mismo pool que la llegada.
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
