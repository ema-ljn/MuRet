#pragma once
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include "World/WorldObjectDrawingHook.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
struct WorldObject;
struct WorldObjectDrawingModel;
struct WorldObjectDrawingPass;
struct WorldObjectDrawingOwner;
struct WorldObjectSpriteRequest;
struct WorldObjectParticleRequest;
struct WorldObjectRuntimeEffectRequest;
struct WorldObjectJointRequest;
// Servicios obligatorios síncronos: no retener referencias ni diferir RNG/emisiones.
// Pool lleno es retorno nativo normal; MissingBinding es error sin rollback.
class IWorldObjectDrawingServices
{
public:
    virtual ~IWorldObjectDrawingServices() = default;
    using Result = Core::Expected<void, WorldObjectDrawingError>;
    [[nodiscard]] virtual Core::Expected<std::reference_wrapper<WorldObjectDrawingModel>, WorldObjectDrawingError>
    Model(std::int16_t type) = 0;
    [[nodiscard]] virtual Result Body(WorldObjectDrawingModel& model, const WorldObjectDrawingPass& pass) = 0;
    [[nodiscard]] virtual Result Mesh(WorldObjectDrawingModel& model, std::int32_t mesh,
                                      const WorldObjectDrawingPass& pass) = 0;
    [[nodiscard]] virtual Result Begin(WorldObjectDrawingModel& model, float alpha) = 0;
    [[nodiscard]] virtual Result End(WorldObjectDrawingModel& model) = 0;
    [[nodiscard]] virtual Result Shadow(WorldObjectDrawingModel& model, std::int32_t first, std::int32_t second,
                                        std::int32_t begin, std::int32_t end) = 0;
    // Índice del buffer GLOBAL de pose; sin matriz, error y nunca identidad ficticia.
    [[nodiscard]] virtual Result Transform(WorldObjectDrawingModel& model, std::size_t bone, glm::vec3& input,
                                           glm::vec3& output) = 0;
    [[nodiscard]] virtual Result AlphaBlend(std::int32_t enabled) = 0;
    [[nodiscard]] virtual Result Color(float red, float green, float blue, std::optional<float> alpha) = 0;
    [[nodiscard]] virtual Core::Expected<float, WorldObjectDrawingError> TerrainHeight(float x, float y) = 0;
    [[nodiscard]] virtual Result Sprite(const WorldObjectSpriteRequest& request, Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Result Particle(const WorldObjectParticleRequest& request,
                                          Render::Models::CrtRandom& random) = 0;
    // No leer UB ni fabricar negro para L indeterminada602ed0/map55/t84. El binding622/10 sólo
    // puede aceptarla si su contrato de creador acredita que no lee ese input.
    [[nodiscard]] virtual Result ParticleWithIndeterminateLight(std::int32_t type, WorldObject& object,
                                                                std::int32_t subtype, float scale,
                                                                Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Result RuntimeEffect(const WorldObjectRuntimeEffectRequest& request,
                                               Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Result Joint(const WorldObjectJointRequest& request, Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Result Action(WorldObject& object, std::uint8_t action) = 0;
    // H5 sólo define low8, H3 conserva mode upper24; knownBits evita fabricar esos upper24.
    [[nodiscard]] virtual Core::Expected<std::uint32_t, WorldObjectDrawingError>
    Handler(WorldObjectDrawingHook hook, WorldObject& object, WorldObjectDrawingModel& model, std::uint32_t argument,
            std::uint32_t knownBits, Render::Models::CrtRandom& random) = 0;
    [[nodiscard]] virtual Core::Expected<std::reference_wrapper<WorldObjectDrawingOwner>, WorldObjectDrawingError>
    OwnerEntry(std::size_t index) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint32_t, WorldObjectDrawingError> OwnerQuery() = 0;
    [[nodiscard]] virtual Result Transition(WorldObjectDrawingModel& model, std::int32_t type) = 0;
    [[nodiscard]] virtual Result Sound(std::int32_t id, std::int32_t first, std::int32_t second) = 0;
    [[nodiscard]] virtual Result Type183(WorldObjectDrawingModel& model, WorldObject& object) = 0;
    [[nodiscard]] virtual Core::Expected<std::uint32_t, WorldObjectDrawingError>
    ProtectedValue(WorldObject& object) = 0;
};
} // namespace Client::World
