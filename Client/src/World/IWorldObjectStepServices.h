#pragma once

#include "Core/Expected.h"
#include "World/WorldObjectStepError.h"
#include "World/WorldObjectStepHook.h"

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
struct WorldObjectParticleRequest;
struct WorldObjectModelEffectRequest;
struct WorldObject;

class IWorldObjectStepServices
{
public:
    virtual ~IWorldObjectStepServices() = default;
    // Binding requerido al pool real; no retener las referencias ni diferir el productor.
    // Pool lleno es éxito nativo: conserva el consumo previo y permite la luz posterior.
    // Unsupported es error explícito de integración; no se presenta como una emisión exitosa.
    [[nodiscard]] virtual Core::Expected<void, WorldObjectStepError>
    CreateParticle(const WorldObjectParticleRequest& request, Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Core::Expected<void, WorldObjectStepError>
    CreateModelEffect(const WorldObjectModelEffectRequest& request, WorldObject& object,
                      Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Core::Expected<void, WorldObjectStepError> PlaySound(std::int32_t sound, std::int32_t first,
                                                                               std::int32_t second) = 0;
    [[nodiscard]] virtual Core::Expected<void, WorldObjectStepError>
    PlayObjectSound(std::int32_t sound, WorldObject& object, std::int32_t second) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint32_t, WorldObjectStepError>
    HandleMap(WorldObjectStepHook hook, WorldObject& object, Render::Models::CrtRandom& random) = 0;
};
} // namespace Client::World
