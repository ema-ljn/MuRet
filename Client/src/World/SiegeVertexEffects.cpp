#include "World/SiegeVertexEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
constexpr std::int16_t FirstGateFragment = 0x12f, LastGateFragment = 0x130;
constexpr std::int16_t FirstStatueFragment = 0x131, LastStatueFragment = 0x133;
constexpr double Tenth = static_cast<double>(0.1f);
constexpr int Degrees = 360;
constexpr int GatePushMask = 255, StonePushMask = 127, GatePushBase = 128, StatuePushBase = 64;
constexpr int ScaleMask = 1, ScaleBase = 7, StoneScaleSpan = 7, StoneScaleBase = 10;
constexpr int FallSpan = 5, GateFallBase = 6, OtherFallBase = 2, LifeMask = 15, LifeBase = 32;
constexpr double SpawnLift = 50.0;
constexpr double Friction = static_cast<double>(0.9f), Boost = static_cast<double>(1.1f);
constexpr double GateGravity = 6.0, StoneGravity = 3.0, StatueGravity = 4.0;
constexpr double StoneBounce = static_cast<double>(-0.2f), StatueBounce = -0.5;
constexpr double GroundTilt = 128.0, AirTilt = 16.0;
constexpr int GroundWear = 5, DustChance = 10;

float F32(double value)
{
    return static_cast<float>(value);
}

void RotateThePush(ModelEffect& effect, float push, Render::Models::CrtRandom& random)
{
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
    effect.velocity = Render::Models::OriginalVectorRotate(
        {0.0f, push, 0.0f}, Render::Models::OriginalAngleMatrix(effect.transform.angles));
}

void RandomizeTheAngles(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    effect.transform.angles.x = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.y = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
}

void BeginTheGateFragment(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    // 51d4a3..51d5b6: push, escala, gravedad, vida, yaw del empuje, tres ángulos, altura.
    const bool gate = effect.type >= FirstGateFragment;
    const float push =
        F32(static_cast<double>((random.Next() & (gate ? GatePushMask : StonePushMask)) + GatePushBase) * Tenth);
    effect.transform.scale = F32(static_cast<double>(gate ? (random.Next() & ScaleMask) + ScaleBase
                                                          : random.Next() % StoneScaleSpan + StoneScaleBase) *
                                 Tenth);
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + (gate ? GateFallBase : OtherFallBase));
    effect.life = (random.Next() & LifeMask) + LifeBase;
    RotateThePush(effect, push, random);
    RandomizeTheAngles(effect, random);
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + SpawnLift);
}

void BeginTheStatueFragment(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    // 51d5b7..51d744: el131/0 pasa al1 y consume un sorteo más para la gravedad.
    const float push = F32(static_cast<double>((random.Next() & StonePushMask) + StatuePushBase) * Tenth);
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + SpawnLift);
    effect.life = (random.Next() & LifeMask) + LifeBase;
    effect.transform.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * Tenth);
    RotateThePush(effect, push, random);
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + OtherFallBase);
    if (effect.type == FirstStatueFragment && effect.subtype == 0)
    {
        effect.subtype = 1;
        effect.turnDirection = F32(static_cast<double>(random.Next() % FallSpan) + effect.turnDirection);
    }
    else if (effect.subtype == 2)
        effect.transform.scale = F32(static_cast<double>(effect.transform.scale) * static_cast<double>(0.6f));
    RandomizeTheAngles(effect, random);
}

void MoveTheFragment(ModelEffect& effect, bool stoneFamily)
{
    const bool door = effect.type >= FirstGateFragment && stoneFamily;
    for (int axis = 0; axis < 3; ++axis)
    {
        effect.transform.position[axis] =
            F32(static_cast<double>(effect.velocity[axis]) + effect.transform.position[axis]);
        effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Friction);
    }
    if (stoneFamily)
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - (door ? GateGravity : StoneGravity));
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + effect.turnDirection);
    if (!stoneFamily && effect.subtype == 1)
    {
        for (int axis = 0; axis < 3; ++axis)
            effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Boost);
    }
    if (!stoneFamily)
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - StatueGravity);
}

Core::Expected<void, std::string> LandTheFragment(ModelEffect& effect, bool stoneFamily,
                                                  const ModelEffectStepServices& services)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::format("{}: falta la altura del terreno para el fragmento {:#x}",
                                            stoneFamily ? "0x5292a3" : "0x5294ab", effect.type));
    // 5292a8 redondea antes de comparar;5294b0 conserva la altura en la x87.
    const double sampled = services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    const double ground = stoneFamily ? static_cast<double>(F32(sampled)) : sampled;
    // TEST C0 solo en5292b6 también toma la rama de impacto con NaN;5294b8 mira C0|C3.
    const bool hit = stoneFamily ? !(effect.transform.position.z >= ground) : ground > effect.transform.position.z;
    if (hit)
    {
        effect.transform.position.z = F32(ground);
        effect.turnDirection =
            F32(static_cast<double>(effect.turnDirection) * (stoneFamily ? StoneBounce : StatueBounce));
        effect.life -= GroundWear;
    }
    effect.transform.angles.x = F32(static_cast<double>(effect.transform.angles.x) -
                                    static_cast<double>(effect.transform.scale) * (hit ? GroundTilt : AirTilt));
    return {};
}
} // namespace

bool IsSiegeVertexEffect(std::int16_t type)
{
    constexpr std::int16_t FirstStoneFragment = 0x12d;
    return type >= FirstStoneFragment && type <= LastStatueFragment;
}

std::optional<std::size_t> CreateSiegeVertexEffect(const ModelEffectArguments& arguments, ModelEffects& effects,
                                                   Render::Models::CrtRandom& random)
{
    if (!IsSiegeVertexEffect(arguments.type))
        return std::nullopt;
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return slot;
    ModelEffect& effect = effects.Effects()[*slot];
    if (effect.type <= LastGateFragment)
        BeginTheGateFragment(effect, random);
    else
        BeginTheStatueFragment(effect, random);
    effect.previousPosition = effect.transform.position;
    return slot;
}

Core::Expected<void, std::string> StepSiegeVertexEffect(ModelEffect& effect, Particles& particles,
                                                        Render::Models::CrtRandom& random,
                                                        const ModelEffectStepServices& services)
{
    const bool stoneFamily = effect.type <= LastGateFragment;
    MoveTheFragment(effect, stoneFamily);
    if (auto landed = LandTheFragment(effect, stoneFamily, services); !landed.HasValue())
        return landed;
    if (stoneFamily)
    {
        effect.alpha = F32(static_cast<double>(effect.life) * Tenth);
        return {};
    }
    if (random.Next() % DustChance != 0)
        return {};
    if (std::ranges::none_of(particles.All(), [](const Particle& particle) { return !particle.alive; }))
        return {};
    // 529516 pasa LEA[EBP-28], igual que5252e6: ningún camino lo inicializa (Q-C1176).
    return Core::Unexpected(std::string("0x529520: el polvo 0x5f3 del fragmento usa [EBP-0x28] sin inicializar "
                                        "en 0x5208a0 (Q-C1176; requiere evidencia de la VM)"));
}
} // namespace Client::World
