#include "World/KalimaObjectMovement.h"

#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <cmath>
#include <cstdint>

namespace Client::World
{
void MoveKalimaObject(WorldObject& object, const glm::vec3& heroPosition, std::uint8_t heroAction)
{
    constexpr std::uint8_t FirstAction = 0x0d, LastAction = 0x21;
    constexpr double Trigger = static_cast<double>(0.1f), Radius = 50;
    constexpr double Repulsion = static_cast<double>(-0.6f), Damping = static_cast<double>(0.6f);
    if (heroAction < FirstAction || heroAction > LastAction)
        return;
    const auto f32 = [](double value) { return static_cast<float>(value); };
    auto& position = object.transform.position;
    auto& velocity = object.velocity;
    if (!(velocity.x >= Trigger))
    {
        const double dx = static_cast<double>(heroPosition.x) - position.x;
        const double dy = static_cast<double>(heroPosition.y) - position.y;
        const float storedY = f32(dy);
        const double distance = std::sqrt(dx * dx + dy * storedY);
        if (!(distance >= Radius))
        {
            velocity.z = 0;
            velocity.x = f32(dx * Repulsion);
            velocity.y = f32(static_cast<double>(storedY) * Repulsion);
        }
    }
    const double retainedX = static_cast<double>(velocity.x) * Damping;
    velocity.x = f32(retainedX);
    velocity.y = f32(static_cast<double>(velocity.y) * Damping);
    velocity.z = f32(static_cast<double>(velocity.z) * Damping);
    for (int axis = 0; axis < 3; ++axis)
        object.copiedAngles[axis] = f32(static_cast<double>(object.copiedAngles[axis]) * Damping);
    position.x = f32(retainedX + position.x);
    position.y = f32(static_cast<double>(velocity.y) + position.y);
    position.z = f32(static_cast<double>(velocity.z) + position.z);
    for (int axis = 0; axis < 3; ++axis)
        object.transform.angles[axis] =
            f32(static_cast<double>(object.copiedAngles[axis]) + object.transform.angles[axis]);
}
} // namespace Client::World
