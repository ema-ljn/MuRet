#pragma once

#include "World/IWorldObjectCreationServices.h"

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class ModelEffects;
}
namespace Client::Scenes
{
// La creación síncrona de 0x606660 conserva los efectos de Noria en el mismo pool que la llegada.
class MapModelEffectServices final : public World::IWorldObjectCreationServices
{
public:
    MapModelEffectServices(World::ModelEffects& effects, Render::Models::CrtRandom& random);
    bool CreateModelEffect(const World::WorldObjectModelEffectRequest& request,
                           World::WorldObject& source) noexcept override;

private:
    World::ModelEffects& m_effects;
    Render::Models::CrtRandom& m_random;
};
} // namespace Client::Scenes
