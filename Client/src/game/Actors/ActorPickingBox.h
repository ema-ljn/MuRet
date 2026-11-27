#pragma once

#include "Models/RayPicking.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors
{

// Los 48 bytes de +0x13c..+0x168: origen y tres ejes de la caja que publicó el último dibujo.
struct ActorPickingBox
{
    glm::vec3 origin{};
    glm::vec3 axisX{};
    glm::vec3 axisY{};
    glm::vec3 axisZ{};
};

[[nodiscard]] ActorPickingBox PickingBoxOf(const Kernel::ActorState& actor);
[[nodiscard]] ActorPickingBox PickingBoxFor(const glm::vec3& position, const glm::vec3& min, const glm::vec3& max);
void PublishPickingBox(Kernel::ActorState& actor, const glm::vec3& position, const glm::vec3& min,
                       const glm::vec3& max);
[[nodiscard]] Models::Box AxisAlignedBoxOf(const ActorPickingBox& box);
} // namespace Client::Actors
