#include "World/SiegeStoneEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

void ChooseTheTarget(ModelEffect& effect, const ModelEffectArguments& arguments, const glm::vec3& hero,
                     Crt::CrtRandom& random)
{
    if (effect.disturbanceState == 1)
    {
        effect.speed = 29.0f;
        effect.siegeTarget = arguments.angles;
        return;
    }
    constexpr float CellScale = 100.0f;
    const int x = (random.Next() & 15) - 8;
    effect.siegeTarget.x = F32(static_cast<double>(x) * CellScale + hero.x);
    if (effect.type == SiegeStoneEffect)
    {
        effect.siegeTarget.y =
            random.Next() % 5 == 0 ? 11400.0f : F32(static_cast<double>(random.Next() % 33 + 98) * CellScale);
    }
    else
        effect.siegeTarget.y = F32(static_cast<double>(random.Next() % 30 + 80) * CellScale);
    effect.siegeTarget.z = 100.0f;
}

void BeginTheFlight(ModelEffect& effect, const ModelEffectArguments& arguments, const glm::vec3& hero,
                    Crt::CrtRandom& random)
{
    constexpr double SpeedStep = static_cast<double>(0.01f);
    constexpr double DirectionStep = static_cast<double>(0.001f);
    effect.life = 300;
    effect.speed = F32(static_cast<double>(random.Next() % 200) * SpeedStep + 28.0);
    effect.turnDirection = 2.0f;
    effect.transform.scale = 2.0f;
    ChooseTheTarget(effect, arguments, hero, random);
    effect.transform.angles = glm::vec3(0.0f);
    effect.light = glm::vec3(1.0f);
    effect.start = arguments.position;
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(static_cast<double>(effect.siegeTarget[axis]) - effect.start[axis]);
    effect.velocity.x = F32(static_cast<double>(effect.velocity.x) * DirectionStep);
    effect.velocity.y = F32(static_cast<double>(effect.velocity.y) * DirectionStep);
    effect.velocity.z = 5.0f;
}

void BeginTheFragment(ModelEffect& effect, Crt::CrtRandom& random)
{
    effect.life = 50;
    effect.transform.scale =
        F32(static_cast<double>(random.Next() & 7) * static_cast<double>(0.05f) + static_cast<double>(0.7f));
    effect.turnDirection = 10.0f;
    effect.transform.angles.x = static_cast<float>(random.Next() % 360);
    effect.velocity = glm::vec3(0.0f);
    effect.copiedAngles.x = 0.0f;
    effect.copiedAngles.y = 0.0f;
    effect.copiedAngles.z = static_cast<float>(random.Next() % 360);
}
} // namespace

bool IsSiegeStoneEffect(std::int16_t type)
{
    return type == SiegeStoneEffect || type == SiegeIceStoneEffect;
}

Core::Expected<std::optional<std::size_t>, std::string> CreateSiegeStone(const ModelEffectArguments& arguments,
                                                                         ModelEffects& effects,
                                                                         const glm::vec3& heroPosition,
                                                                         Crt::CrtRandom& random)
{
    if (!IsSiegeStoneEffect(arguments.type))
        return Core::Unexpected(std::string("0x51cfa4: el tipo pedido no es un proyectil del castillo"));
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return slot;
    ModelEffect& effect = effects.Effects()[*slot];
    if (effect.subtype <= 1)
        BeginTheFlight(effect, arguments, heroPosition, random);
    else if (effect.type == SiegeStoneEffect && effect.subtype == 2)
        BeginTheFragment(effect, random);
    // 51d165/51d1f0: otros subtipos conservan la vida del uso anterior del lugar, tras el prefijo común.
    return slot;
}
} // namespace Client::World
