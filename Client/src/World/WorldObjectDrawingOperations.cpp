#include "World/WorldObjectDrawingOperations.h"
#include "Core/Expected.h"
#include "World/WorldObjectDrawingError.h"
#include "World/WorldObjectRegistrations.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include "World/IWorldObjectDrawingServices.h"
#include "World/WorldObjectDrawingContext.h"
#include "World/WorldObjectDrawingModel.h"
#include "World/WorldObjectSpriteRequest.h"
#include "World/WorldObjectParticleRequest.h"
#include "World/WorldObjectRuntimeEffectRequest.h"
#include "World/WorldObjects.h"
#include "World/Ftol.h"
#include <cstdint>
namespace Client::World
{
WorldObjectDrawingOperations::WorldObjectDrawingOperations(WorldObject& source, WorldObjectDrawingContext& bindings,
                                                           WorldObjectDrawingModel& metadata)
    : object(source), context(bindings), model(&metadata)
{
}
bool WorldObjectDrawingOperations::Run(Result result)
{
    if (result.HasValue())
        return true;
    m_error = result.Error();
    return false;
}
WorldObjectDrawingOperations::Result WorldObjectDrawingOperations::ResultOf() const
{
    if (m_error)
        return Core::Unexpected(*m_error);
    return {};
}
bool WorldObjectDrawingOperations::Ftol(double input, std::int32_t& output)
{
    const auto value = FtolLowChecked(input);
    if (!value)
        return Run(Core::Unexpected(WorldObjectDrawingError::InvalidTime));
    output = *value;
    return true;
}
WorldObjectDrawingPass WorldObjectDrawingOperations::Pass(std::uint32_t mask, std::int32_t texture) const
{
    return {mask,
            object.alpha,
            object.state.blendMesh,
            object.state.light,
            object.state.uv.x,
            object.state.uv.y,
            object.state.hiddenMesh,
            texture};
}
bool WorldObjectDrawingOperations::Body(std::uint32_t mask, std::int32_t texture)
{
    return Body(Pass(mask, texture));
}
bool WorldObjectDrawingOperations::Body(const WorldObjectDrawingPass& pass)
{
    return Run(context.services.Body(*model, pass));
}
bool WorldObjectDrawingOperations::Mesh(std::int32_t mesh, std::uint32_t mask, std::int32_t texture)
{
    return Mesh(mesh, Pass(mask, texture));
}
bool WorldObjectDrawingOperations::Mesh(std::int32_t mesh, const WorldObjectDrawingPass& pass)
{
    return Run(context.services.Mesh(*model, mesh, pass));
}
bool WorldObjectDrawingOperations::Transform(std::size_t bone, glm::vec3& input, glm::vec3& output)
{
    return Run(context.services.Transform(*model, bone, input, output));
}
bool WorldObjectDrawingOperations::Sprite(std::int32_t type, glm::vec3& position, float scale, glm::vec3& light,
                                          float rotation)
{
    return Run(context.services.Sprite(
        WorldObjectSpriteRequest{type, position, scale, light, object.identity, rotation}, context.random));
}
bool WorldObjectDrawingOperations::Particle(std::int32_t type, glm::vec3& position, glm::vec3& light,
                                            std::int32_t subtype, float scale, WorldObjectIdentity owner)
{
    return Run(context.services.Particle(
        WorldObjectParticleRequest{type, position, object.transform.angles, light, subtype, scale, owner},
        context.random));
}
bool WorldObjectDrawingOperations::Effect(std::int32_t type, glm::vec3& position, glm::vec3& light,
                                          std::int32_t subtype, WorldObjectIdentity owner)
{
    return Run(context.services.RuntimeEffect(
        WorldObjectRuntimeEffectRequest{type, position, object.transform.angles, light, subtype, owner},
        context.random));
}
} // namespace Client::World
