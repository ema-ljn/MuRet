#include "Scenes/DamageNumbers.h"

#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Scenes
{
namespace
{
constexpr double DamageHeight = 140.0;
constexpr float DamageSpeed = 10.0f;
} // namespace

void CreateDamageNumber(WorldState& world, const glm::vec3& position, std::int32_t amount, const glm::vec3& light,
                        float scale, bool rise)
{
    for (DamageNumberState& number : world.DrawTail().damage)
    {
        if (number.alive)
            continue;
        number.alive = true;
        number.number = amount;
        number.position = position;
        number.position.z = static_cast<float>(static_cast<double>(position.z) + DamageHeight);
        number.light = light;
        number.scale = scale;
        number.speed = DamageSpeed;
        number.timer = 0;
        number.rise = rise;
        return;
    }
}
} // namespace Client::Scenes
