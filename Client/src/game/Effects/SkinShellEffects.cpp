#include "Effects/SkinShellEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Effects
{
namespace
{
using Math::D;
using Math::F32;

constexpr double Tenth = Math::TenthAsDouble; // 0x6b87b8 y 0x6b8a34, 0,1f
constexpr int Degrees = 360;                  // 0x168

float RandomAngle(Crt::CrtRandom& random)
{
    return static_cast<float>(random.Next() % Degrees);
}
} // namespace

std::optional<std::size_t> CreateSkinShell(const Kernel::ModelEffectArguments& arguments, ModelEffects& effects,
                                           Crt::CrtRandom& random)
{
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return slot;
    ModelEffect& effect = effects.Effects()[*slot];
    constexpr int PushMask = 127, PushBase = 32, LifeMask = 15, LifeBase = 32;
    constexpr int ScaleSpan = 3, ScaleBase = 3, FallSpan = 5, FallBase = 2;
    constexpr double Lift = 50.0; // 0x6b86b0
    // 0x51d8c4..0x51d8f2: el empuje en y, [ebp−0x8]; x y z en 0.
    const float push = F32(static_cast<double>((random.Next() & PushMask) + PushBase) * Tenth);
    effect.transform.position.z = F32(D(effect.transform.position.z) + Lift);
    effect.life = (random.Next() & LifeMask) + LifeBase;
    effect.transform.scale = F32(static_cast<double>(random.Next() % ScaleSpan + ScaleBase) * Tenth);
    effect.alpha = 0.5f; // 0x51d93b, 0x3f000000
    // 0x51d941..0x51d973: la z de los ángulos y el empuje girado por los ángulos (0x5fc8f0, 0x5fcbd0).
    effect.transform.angles.z = RandomAngle(random);
    effect.velocity =
        Math::OriginalVectorRotate({0.0f, push, 0.0f}, Math::OriginalAngleMatrix(effect.transform.angles));
    // 0x51d97b..0x51d9b0: la caída, en dos sorteos guardados en float.
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + FallBase);
    effect.turnDirection = F32(static_cast<double>(random.Next() % FallSpan) + D(effect.turnDirection));
    effect.transform.angles.x = RandomAngle(random);
    effect.transform.angles.y = RandomAngle(random);
    effect.transform.angles.z = RandomAngle(random);
    // 0x51d9f8..0x51da36.
    if (effect.subtype == 0)
        effect.light = glm::vec3(0.5f);
    else if (effect.subtype == 1)
        effect.light = {std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f19999au), 1.0f};
    effect.previousPosition = effect.transform.position;
    return slot;
}

Core::Expected<void, std::string> StepSkinShell(ModelEffect& effect, const ModelEffectStepServices& services)
{
    constexpr double Friction = static_cast<double>(std::bit_cast<float>(0x3f666666u)); // 0x6b8a1c, 0,9f
    constexpr double Boost = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu));    // 0x6b8830, 1,1f
    constexpr double Gravity = 3.0;                                                     // 0x6b86b8
    constexpr double Bounce = static_cast<double>(std::bit_cast<float>(0xbe4ccccdu));   // 0x6b8d3c, −0,2f
    constexpr double GroundTilt = 128.0, AirTilt = 16.0;                                // 0x6b8844 y 0x6b8f00
    constexpr std::int32_t GroundWear = 5;
    glm::vec3& position = effect.transform.position;
    // 0x529926..0x529947: x + vx, y + vy y z + vz.
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(effect.velocity[axis]) + D(position[axis]));
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(D(effect.velocity[axis]) * Friction);
    position.z = F32(D(position.z) + D(effect.turnDirection));
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(D(effect.velocity[axis]) * Boost);
    effect.turnDirection = F32(D(effect.turnDirection) - Gravity);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x5299dc: falta la altura del terreno para el 0x137"));
    // 0x5299e1..0x5299ec: fcom de la altura (en la x87) con la z; test ah,0x41: sólo con el terreno estrictamente
    // arriba.
    const double ground = services.terrainHeight(position.x, position.y);
    const bool landed = ground > D(position.z);
    if (landed)
    {
        position.z = F32(ground);
        effect.turnDirection = F32(D(effect.turnDirection) * Bounce);
        effect.life -= GroundWear;
    }
    effect.transform.angles.x =
        F32(D(effect.transform.angles.x) - D(effect.transform.scale) * (landed ? GroundTilt : AirTilt));
    effect.alpha = F32(static_cast<double>(effect.life) * Tenth);
    return {};
}
} // namespace Client::Effects
