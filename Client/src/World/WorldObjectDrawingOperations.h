#pragma once
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include "World/WorldObjectDrawingPass.h"
#include "World/WorldObjectRegistrations.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace Client::World
{
struct WorldObject;
struct WorldObjectDrawingContext;
struct WorldObjectDrawingModel;
// Operaciones comunes de los dos cuerpos; guarda sólo el primer error, sin rollback.
class WorldObjectDrawingOperations
{
public:
    WorldObjectDrawingOperations(WorldObject& object, WorldObjectDrawingContext& context,
                                 WorldObjectDrawingModel& model);
    using Result = Core::Expected<void, WorldObjectDrawingError>;
    bool Run(Result result);
    [[nodiscard]] Result ResultOf() const;
    bool Ftol(double input, std::int32_t& output);
    [[nodiscard]] WorldObjectDrawingPass Pass(std::uint32_t mask, std::int32_t texture = -1) const;
    bool Body(std::uint32_t mask, std::int32_t texture = -1);
    bool Body(const WorldObjectDrawingPass& pass);
    bool Mesh(std::int32_t mesh, std::uint32_t mask, std::int32_t texture = -1);
    bool Mesh(std::int32_t mesh, const WorldObjectDrawingPass& pass);
    bool Transform(std::size_t bone, glm::vec3& input, glm::vec3& output);
    bool Sprite(std::int32_t type, glm::vec3& position, float scale, glm::vec3& light, float rotation = 0);
    bool Particle(std::int32_t type, glm::vec3& position, glm::vec3& light, std::int32_t subtype, float scale,
                  WorldObjectIdentity owner = 0);
    bool Effect(std::int32_t type, glm::vec3& position, glm::vec3& light, std::int32_t subtype,
                WorldObjectIdentity owner = 0);
    WorldObject& object;
    WorldObjectDrawingContext& context;
    WorldObjectDrawingModel* model;

private:
    std::optional<WorldObjectDrawingError> m_error;
};
// El buffer global empieza en68b181c; stride30, sin sustituir huesos ausentes por identidad.
[[nodiscard]] constexpr std::size_t WorldObjectPoseBone(std::uint32_t address)
{
    return (address - 0x068b181cu) / 0x30u;
}
} // namespace Client::World
