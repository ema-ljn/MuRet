#include "Scenes/ActorPickingBox.h"

#include "Models/RayPicking.h"
#include "Scenes/ActorState.h"

#include <array>

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>

namespace Client::Scenes
{
ActorPickingBox PickingBoxOf(const ActorState& actor)
{
    return {.origin = {actor.field13c, actor.field140, actor.field144},
            .axisX = {actor.field148, actor.field14c, actor.field150},
            .axisY = {actor.field154, actor.field158, actor.field15c},
            .axisZ = {actor.field160, actor.field164, actor.field168}};
}

ActorPickingBox PickingBoxFor(const glm::vec3& position, const glm::vec3& min, const glm::vec3& max)
{
    return {.origin = position + min,
            .axisX = {max.x - min.x, 0.0f, 0.0f},
            .axisY = {0.0f, max.y - min.y, 0.0f},
            .axisZ = {0.0f, 0.0f, max.z - min.z}};
}

void PublishPickingBox(ActorState& actor, const glm::vec3& position, const glm::vec3& min, const glm::vec3& max)
{
    const ActorPickingBox box = PickingBoxFor(position, min, max);
    actor.field13c = box.origin.x;
    actor.field140 = box.origin.y;
    actor.field144 = box.origin.z;
    actor.field148 = box.axisX.x;
    actor.field14c = box.axisX.y;
    actor.field150 = box.axisX.z;
    actor.field154 = box.axisY.x;
    actor.field158 = box.axisY.y;
    actor.field15c = box.axisY.z;
    actor.field160 = box.axisZ.x;
    actor.field164 = box.axisZ.y;
    actor.field168 = box.axisZ.z;
}

Models::Box AxisAlignedBoxOf(const ActorPickingBox& box)
{
    glm::vec3 min = box.origin;
    glm::vec3 max = box.origin;
    for (const glm::vec3 axis : std::array{box.axisX, box.axisY, box.axisZ})
    {
        min += glm::min(axis, glm::vec3(0.0f));
        max += glm::max(axis, glm::vec3(0.0f));
    }
    return {.min = min, .max = max};
}
} // namespace Client::Scenes
