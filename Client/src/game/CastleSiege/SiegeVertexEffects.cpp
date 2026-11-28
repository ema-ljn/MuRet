#include "CastleSiege/SiegeVertexEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::CastleSiege
{
namespace
{
constexpr std::int16_t FirstStoneFragment = 0x12d, SecondStoneFragment = 0x12e;
constexpr std::int16_t FirstGateFragment = 0x12f, LastGateFragment = 0x130;
constexpr std::int16_t FirstStatueFragment = 0x131, LastStatueFragment = 0x133;
// Skill\golem_stone (0x616894): the body stones that 0x4ee770 drops when the 0x1a4 dies (0x441696).
constexpr std::int16_t GolemStone = 0x13c;
// The pebbles 0x12d and 0x12e of /2 (the ones 0x4ee770 drops instead of the 0x13c with subtype 1, 0x4eee35) and the
// 0x12e/3 (the one the 0x1a6 throws in its attack, 0x442397).
constexpr std::int32_t ShowerSubtype = 2, ThrownSubtype = 3;
constexpr double Tenth = Math::TenthAsDouble;
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

using Math::F32;

void RotateThePush(Effects::ModelEffect& effect, float push, Crt::CrtRandom& random)
{
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
    effect.velocity =
        Math::OriginalVectorRotate({0.0f, push, 0.0f}, Math::OriginalAngleMatrix(effect.transform.angles));
}

void RandomizeTheAngles(Effects::ModelEffect& effect, Crt::CrtRandom& random)
{
    effect.transform.angles.x = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.y = static_cast<float>(random.Next() % Degrees);
    effect.transform.angles.z = static_cast<float>(random.Next() % Degrees);
}

// The stone family: the one 0x5291ce moves with the fall of +0xe4 and 0x51d50d finishes creating.
bool IsStoneFragment(std::int16_t type)
{
    return (type >= FirstStoneFragment && type <= LastGateFragment) || type == GolemStone;
}

bool IsShower(const Effects::ModelEffect& effect)
{
    return (effect.type == FirstStoneFragment || effect.type == SecondStoneFragment) && effect.subtype == ShowerSubtype;
}

bool IsThrownStone(const Effects::ModelEffect& effect)
{
    return effect.type == SecondStoneFragment && effect.subtype == ThrownSubtype;
}

void BeginTheGateFragment(Effects::ModelEffect& effect, Crt::CrtRandom& random)
{
    // 0x51d2a9..0x51d50d, each one's part, one rand() per field: the push (in y), the scale and the fall (+0xe4). The
    // gate (0x12f, 0x130): ((r & 255) + 128) · 0.1f, ((r & 1) + 7) · 0.1f and r % 5 + 6. The 0x13c (0x51d32c): ((r &
    // 127) + 32) · 0.1f, ((r & 1) + 10) · 0.25 and r % 5 + 2. The 0x12d/2 and the 0x12e/2 (0x51d3b1): ((r & 127) + 32)
    // · 0.1f,
    // ((r & 1) + 4) · 0.12f + 0.6f and r % 5 + 2. The others (0x51d4a5): ((r & 127) + 128) · 0.1f, (r % 7 + 10) · 0.1f
    // and r % 5 + 2. 0x51d50d..0x51d5b6, for all: life, push yaw, three angles, height.
    constexpr int SmallPushBase = 32, GolemScaleBase = 10, ShowerScaleBase = 4;
    constexpr double GolemScaleStep = 0.25;                                                     // 0x6b89e0
    constexpr double ShowerScaleStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));  // 0x6ba1d8, 0.12f
    constexpr double ShowerScaleFloor = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20, 0.6f
    const bool gate = effect.type >= FirstGateFragment && effect.type <= LastGateFragment;
    const bool golem = effect.type == GolemStone;
    const bool shower = IsShower(effect);
    const bool small = golem || shower;
    const float push = F32(static_cast<double>((random.Next() & (gate ? GatePushMask : StonePushMask)) +
                                               (small ? SmallPushBase : GatePushBase)) *
                           Tenth);
    if (golem)
        effect.transform.scale =
            F32(static_cast<double>((random.Next() & ScaleMask) + GolemScaleBase) * GolemScaleStep);
    else if (shower)
        effect.transform.scale = F32(
            static_cast<double>((random.Next() & ScaleMask) + ShowerScaleBase) * ShowerScaleStep + ShowerScaleFloor);
    else
        effect.transform.scale = F32(static_cast<double>(gate ? (random.Next() & ScaleMask) + ScaleBase
                                                              : random.Next() % StoneScaleSpan + StoneScaleBase) *
                                     Tenth);
    effect.turnDirection = static_cast<float>(random.Next() % FallSpan + (gate ? GateFallBase : OtherFallBase));
    effect.life = (random.Next() & LifeMask) + LifeBase;
    RotateThePush(effect, push, random);
    RandomizeTheAngles(effect, random);
    effect.transform.position.z = F32(static_cast<double>(effect.transform.position.z) + SpawnLift);
}

// 0x51d438..0x51d4a4, the 0x12e/3: life (r & 15) + 32, scale (r % 3 + 2) · 0.12f + 1.2f, z speed at −20 − r % 5
// and +0xd8 at 1.8f. Without the others' tail: neither the angles nor the height.
void BeginTheThrownStone(Effects::ModelEffect& effect, Crt::CrtRandom& random)
{
    constexpr int ThrownScaleSpan = 3, ThrownScaleBase = 2, DropSpan = 5, DropBase = -20;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));  // 0x6ba1d8, 0.12f
    constexpr double ScaleFloor = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1.2f
    constexpr float Spin = std::bit_cast<float>(0x3fe66666u);                             // 0x51d496, 1.8f
    effect.life = (random.Next() & LifeMask) + LifeBase;
    effect.transform.scale =
        F32(static_cast<double>(random.Next() % ThrownScaleSpan + ThrownScaleBase) * ScaleStep + ScaleFloor);
    effect.velocity.z = static_cast<float>(DropBase - random.Next() % DropSpan);
    effect.speed = Spin;
}

void BeginTheStatueFragment(Effects::ModelEffect& effect, Crt::CrtRandom& random)
{
    // 0x51d5b7..0x51d744: the 0x131/0 goes to 1 and draws one more rand() for the gravity.
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

void MoveTheFragment(Effects::ModelEffect& effect, bool stoneFamily)
{
    // 0x5291f8: the fall of 6 is only the gate's; the 0x13c falls by 3 like the pebbles.
    const bool door = effect.type >= FirstGateFragment && effect.type <= LastGateFragment;
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

// Returns the terrain height as the original left it (the stones' one, stored as a float at [ebp+8]).
Core::Expected<double, std::string> LandTheFragment(Effects::ModelEffect& effect, bool stoneFamily,
                                                    const Effects::ModelEffectStepServices& services)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::format("{}: falta la altura del terreno para el fragmento {:#x}",
                                            stoneFamily ? "0x5292a3" : "0x5294ab", effect.type));
    // 0x5292a8 rounds before comparing; 0x5294b0 keeps the height on the x87.
    const double sampled = services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    const double ground = stoneFamily ? static_cast<double>(F32(sampled)) : sampled;
    // TEST C0 alone at 0x5292b6 also takes the impact branch with NaN; 0x5294b8 checks C0|C3.
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
    return ground;
}

// 0x53fe70 with the owner at 0: a type or subtype that Particles::Particles lacks stops with the address (with the
// table full it creates nothing, takes no rand() and the step goes on).
Core::Expected<void, std::string> Emit(Particles::Particles& particles, std::int32_t type,
                                       const Effects::ModelEffect& effect, const glm::vec3& light, std::int32_t subtype,
                                       float scale, float worldClock, Crt::CrtRandom& random, std::uint32_t address)
{
    const auto made = particles.CreateChecked(type, effect.transform.position, effect.transform.angles, light, subtype,
                                              scale, worldClock, random);
    if (!made.HasValue())
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/{} del fragmento {:#x} falla con el error {} "
                                            "(CL11)",
                                            address, type, subtype, effect.type, static_cast<unsigned>(made.Error())));
    return {};
}

// The step's local light ([ebp−0x1c], 0x520921).
constexpr glm::vec3 StepLight{1.0f};

// 0x52930e..0x529385, the 0x13c after falling: with rand() & 3 at 0, stone 0x6ec/5 (scale 2.8f) and smoke
// 0x5f2/0x15 (1.8f); with rand() % 10 at 0, dust 0x5f3/0 (1). All with the local light.
Core::Expected<void, std::string> CrumbleTheGolemStone(const Effects::ModelEffect& effect,
                                                       Particles::Particles& particles, Crt::CrtRandom& random,
                                                       float worldClock)
{
    constexpr std::int32_t StoneParticle = 0x6ec, StoneSubtype = 5, SmokeSubtype = 0x15;
    constexpr float StoneScale = std::bit_cast<float>(0x40333333u); // 0x529323, 2.8f
    constexpr float SmokeScale = std::bit_cast<float>(0x3fe66666u); // 0x52933c, 1.8f
    constexpr int StoneChance = 3;
    if ((random.Next() & StoneChance) == 0)
    {
        if (auto made = Emit(particles, StoneParticle, effect, StepLight, StoneSubtype, StoneScale, worldClock, random,
                             0x529335);
            !made.HasValue())
            return made;
        if (auto made = Emit(particles, Particles::BreathParticle, effect, StepLight, SmokeSubtype, SmokeScale,
                             worldClock, random, 0x52934e);
            !made.HasValue())
            return made;
    }
    if (random.Next() % DustChance != 0)
        return {};
    return Emit(particles, Particles::DustParticle, effect, StepLight, 0, 1.0f, worldClock, random, 0x52937d);
}

// 0x52938a..0x5293ea, the 0x12d/2: with rand() % 10 at 0, z at this step's terrain height and smoke 0x5f2/0xb
// (scale 2) with the local light at (0.2f; 0.5f; 0.35f).
Core::Expected<void, std::string> SmokeTheShower(Effects::ModelEffect& effect, double ground,
                                                 Particles::Particles& particles, Crt::CrtRandom& random,
                                                 float worldClock)
{
    constexpr glm::vec3 SmokeLight{std::bit_cast<float>(0x3e4ccccdu), 0.5f, std::bit_cast<float>(0x3eb33333u)};
    constexpr float SmokeScale = 2.0f; // 0x5293b8
    if (random.Next() % DustChance != 0)
        return {};
    effect.transform.position.z = F32(ground);
    return Emit(particles, Particles::BreathParticle, effect, SmokeLight, Particles::StoneDustSubtype, SmokeScale,
                worldClock, random, 0x5293e2);
}

// 0x5290df..0x5291c9, the 0x12e/3: the z speed goes down by +0xd8, which goes up by 0.3f. With the terrain (0x5f93c0,
// unrounded) above its z, z at the terrain, the speed, the angles and +0x58 at 0 and, with rand() % 10 at 0, smoke
// 0x5f2/0x18 with the scale · 1.25; otherwise (also with NaN), with rand() & 1 at 0, the same smoke with 0.2f. Both
// with their light (+0xf4).
Core::Expected<void, std::string> StepTheThrownStone(Effects::ModelEffect& effect, Particles::Particles& particles,
                                                     Crt::CrtRandom& random,
                                                     const Effects::ModelEffectStepServices& services, float worldClock)
{
    constexpr double SpinGain = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc, 0.3f
    constexpr double LandedSmoke = 1.25;                                                // 0x6b8ad4
    constexpr float FallingSmoke = std::bit_cast<float>(0x3e4ccccdu);                   // 0x5291a9, 0.2f
    constexpr std::int32_t SmokeSubtype = 0x18;
    effect.velocity.z = F32(static_cast<double>(effect.velocity.z) - static_cast<double>(effect.speed));
    effect.speed = F32(static_cast<double>(effect.speed) + SpinGain);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x529117: falta la altura del terreno para el 0x12e/3"));
    const double ground = services.terrainHeight(effect.transform.position.x, effect.transform.position.y);
    if (ground > static_cast<double>(effect.transform.position.z))
    {
        effect.transform.position.z = F32(ground);
        effect.velocity = glm::vec3(0.0f);
        effect.transform.angles = glm::vec3(0.0f);
        effect.objectState.hiddenMesh = 0;
        if (random.Next() % DustChance != 0)
            return {};
        const float scale = F32(static_cast<double>(effect.transform.scale) * LandedSmoke);
        return Emit(particles, Particles::BreathParticle, effect, effect.light, SmokeSubtype, scale, worldClock, random,
                    0x529181);
    }
    if ((random.Next() & 1) != 0)
        return {};
    return Emit(particles, Particles::BreathParticle, effect, effect.light, SmokeSubtype, FallingSmoke, worldClock,
                random, 0x5291c1);
}
} // namespace

bool IsSiegeVertexEffect(std::int16_t type)
{
    return (type >= FirstStoneFragment && type <= LastStatueFragment) || type == GolemStone;
}

std::optional<std::size_t> CreateSiegeVertexEffect(const Kernel::ModelEffectArguments& arguments,
                                                   Effects::ModelEffects& effects, Crt::CrtRandom& random)
{
    if (!IsSiegeVertexEffect(arguments.type))
        return std::nullopt;
    const auto slot = effects.Begin(arguments);
    if (!slot)
        return slot;
    Effects::ModelEffect& effect = effects.Effects()[*slot];
    if (IsThrownStone(effect))
        BeginTheThrownStone(effect, random);
    else if (IsStoneFragment(effect.type))
        BeginTheGateFragment(effect, random);
    else
        BeginTheStatueFragment(effect, random);
    effect.previousPosition = effect.transform.position;
    return slot;
}

Core::Expected<void, std::string> StepSiegeVertexEffect(Effects::ModelEffect& effect, Particles::Particles& particles,
                                                        Crt::CrtRandom& random,
                                                        const Effects::ModelEffectStepServices& services,
                                                        float worldClock)
{
    if (IsThrownStone(effect))
        return StepTheThrownStone(effect, particles, random, services, worldClock);
    const bool stoneFamily = IsStoneFragment(effect.type);
    MoveTheFragment(effect, stoneFamily);
    const auto landed = LandTheFragment(effect, stoneFamily, services);
    if (!landed.HasValue())
        return Core::Unexpected(landed.Error());
    if (stoneFamily)
    {
        effect.alpha = F32(static_cast<double>(effect.life) * Tenth);
        if (effect.type == GolemStone)
            return CrumbleTheGolemStone(effect, particles, random, worldClock);
        if (effect.type == FirstStoneFragment && effect.subtype == ShowerSubtype)
            return SmokeTheShower(effect, landed.Value(), particles, random, worldClock);
        return {};
    }
    if (random.Next() % DustChance != 0)
        return {};
    if (std::ranges::none_of(particles.All(), [](const Particles::Particle& particle) { return !particle.alive; }))
        return {};
    // 0x529516 passes LEA [EBP-0x28], like 0x5252e6: no path initializes it (Q-C1176).
    return Core::Unexpected(std::string("0x529520: el polvo 0x5f3 del fragmento usa [EBP-0x28] sin inicializar "
                                        "en 0x5208a0 (Q-C1176; requiere evidencia de la VM)"));
}
} // namespace Client::CastleSiege
