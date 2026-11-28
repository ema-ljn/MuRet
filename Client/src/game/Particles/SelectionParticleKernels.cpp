#include "Particles/SelectionParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::Particles
{
namespace
{
// 0x6b86c0 and 0x6b88d4 (0x3c23d70a), 0x6b87b8 and 0x6b8a34 (0x3dcccccd), 0x6b8a48, 0x6b8bf8, 0x6b882c.
constexpr double Hundredth = Math::HundredthAsDouble;
constexpr double Tenth = Math::TenthAsDouble;
constexpr double Twentieth = Math::TwentiethAsDouble;
constexpr double FlameRiseBase = static_cast<double>(std::bit_cast<float>(0x3fe66666u));
constexpr double Half = Math::HalfAsDouble;
// 0x5476f0..0x547771: 0x6ba394 (0.0002f), 0x6b86d0 (20), 0x6b8b90 (0.02f) and 0x6ba354 (−0.02f).
constexpr double MistPhase = static_cast<double>(std::bit_cast<float>(0x3951b717u));
constexpr float MistAmplitude = 20.0f;
constexpr double MistTurn = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
// 0x545e3f..0x545ea9: 0x6b8818 (2), 0x6ba3b8 and 0x6ba3b4.
constexpr double FlameTurn = 2.0;
constexpr double FlameShrink = static_cast<double>(std::bit_cast<float>(0x3c2c7692u));
constexpr double FlameFade = static_cast<double>(std::bit_cast<float>(0x3f7e3870u));
// 0x547880..0x5478cf: 0x6ba350 (1.16f) and 0x6ba34c.
constexpr double ChaseBrighten = static_cast<double>(std::bit_cast<float>(0x3f947ae1u));
constexpr double ChaseFade = static_cast<double>(std::bit_cast<float>(0x3f5cb08eu));
constexpr double ChaseLift = 250.0;
constexpr int MistLife = 30, MistSeenLife = 50, FlameLife = 33, ChaseFadeLife = 10;
// 0x6b8ae8 (0.15f)
constexpr double FloorFlameRise = static_cast<double>(std::bit_cast<float>(0x3e19999au));
constexpr int FullTurn = Math::FullTurn;

using Math::F32;

void ScaleLight(Particle& p, double factor)
{
    for (int axis = 0; axis < 3; ++axis)
        p.light[axis] = F32(static_cast<double>(p.light[axis]) * factor);
}

// 0x5435cf..0x54363b (the else of 0x622, subtype 1), and the exit 0x5435bb that clears the velocity.
void CreateOwnedMist(Particle& p, float scaleArgument, Crt::CrtRandom& random)
{
    p.life = MistLife;
    p.verticalSpeed = static_cast<float>(random.Next() % 1000);
    constexpr int Spread = 500, Center = 250, RiseSpan = 20, RiseBase = 20;
    p.position.x = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.x);
    p.position.y = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.y);
    p.position.z = F32(static_cast<double>(random.Next() % RiseSpan + RiseBase) + p.position.z);
    const int phase = random.Next() % 100;
    p.start.z = p.position.z;
    p.start.y = F32(static_cast<double>(phase) * Hundredth);
    constexpr int ScaleSpan = 20, ScaleBase = 180;
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan + ScaleBase) * Hundredth);
    constexpr int TurnSpan = 30;
    p.rotationFactor = F32(static_cast<double>(random.Next() % TurnSpan) * Hundredth + scaleArgument);
    p.velocity = glm::vec3(0.0f);
}

// 0x54150e..0x5415fb: the start x in +0x6c before the turn; the y follows the x's shift.
void CreateRisingFlame(Particle& p, Crt::CrtRandom& random)
{
    p.life = FlameLife;
    constexpr int ShrinkSpan = 20;
    p.scale = F32(static_cast<double>(p.scale) - static_cast<double>(random.Next() % ShrinkSpan) * Hundredth);
    const int turn = random.Next() % FullTurn;
    p.start.x = p.position.x;
    constexpr double HalfTurn = Math::HalfTurnAsDouble;
    p.rotation = F32(static_cast<double>(turn) - HalfTurn);
    const int side = random.Next() & 1;
    p.position.x = F32(static_cast<double>(side) * Half * p.scale + p.position.x);
    p.verticalSpeed = F32((static_cast<double>(random.Next() % 100) * Hundredth + FlameRiseBase) * p.scale);
    constexpr int Still = 3, StillSpan = 10, DriftSpan = 20;
    float drift = 0.0f;
    if (random.Next() % StillSpan >= Still)
        drift = F32((static_cast<double>(random.Next() % DriftSpan) * Tenth - 1.0) * p.scale);
    p.velocity = glm::vec3(0.0f, drift, 0.0f);
    p.position.y = F32((static_cast<double>(p.position.x) - p.start.x) + p.position.y);
}

// 0x54143c..0x54149d: the flame of the 0x5de jet of 4 rises straight; its scale ((& 63) + 64) · argument · 0.01f is
// rounded once.
void CreateFloorFlame(Particle& p, float scaleArgument, Crt::CrtRandom& random)
{
    constexpr int FloorFlameLife = 20, RiseMask = 127, RiseBase = 128, SizeMask = 63, SizeBase = 64;
    p.life = FloorFlameLife;
    p.velocity.x = 0.0f;
    p.velocity.y = 0.0f;
    p.velocity.z = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * FloorFlameRise);
    p.scale = F32((static_cast<double>((random.Next() & SizeMask) + SizeBase) * static_cast<double>(scaleArgument)) *
                  Hundredth);
}

// 0x543840..0x543931: /1 is drawn with Impack03 and rises; both roll life and position alike.
void CreateChaseLight(Particle& p, Crt::CrtRandom& random)
{
    constexpr int SizeSpan = 10, SizeBase = 20, WideSpan = 5;
    if (p.subtype == 1)
    {
        p.texture = ChaseLightImpactTexture;
        p.scale = F32(static_cast<double>(random.Next() % SizeSpan + SizeBase) * Hundredth);
        p.verticalSpeed = F32(static_cast<double>(random.Next() % SizeSpan + SizeBase) * Twentieth);
    }
    else
    {
        p.verticalSpeed = 0.0f;
        p.scale = F32(static_cast<double>(random.Next() % WideSpan + SizeBase) * Tenth);
    }
    constexpr int LifeSpan = 3, LifeBase = 20, Spread = 50, Center = 25, HeightSpan = 200, HeightCenter = 100;
    p.life = random.Next() % LifeSpan + LifeBase;
    p.position.x = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.x);
    p.position.y = F32(static_cast<double>(random.Next() % Spread - Center) + p.position.y);
    p.position.z = F32((static_cast<double>(random.Next() % HeightSpan - HeightCenter) + p.position.z) + ChaseLift);
}

Core::Expected<void, ParticleError> StepOwnedMist(Particle& p, const float* worldClock,
                                                  IParticleTrigonometry* trigonometry,
                                                  const ParticleObjectOwners& owners)
{
    WorldObjects::WorldObject* owner = nullptr;
    if (p.typedOwner.kind == ParticleOwnerKind::WorldObject && owners)
        owner = owners(p.typedOwner.identity);
    if (owner == nullptr)
        return Core::Unexpected(ParticleError::MissingOwner);
    if (worldClock == nullptr || trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    // 0x5476f0..0x547711: a seen owner renews the life; with no life, the owner emits again (+0x58 = 0).
    if (owner->seen)
        p.life = MistSeenLife;
    if (p.life <= 0)
    {
        owner->state.hiddenMesh = 0;
        p.alive = false;
    }
    const double argument = (static_cast<double>(*worldClock) + p.verticalSpeed) * MistPhase;
    const auto sine = trigonometry->SineProductPC53(argument, MistAmplitude);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    p.position.z = F32(sine.Value() + p.start.z);
    // 0x547731 (table 0x54848c): 1 and 4 turn forward, 2 and 5 backward (0x6ba354), and 0 and 3 leave the turn alone,
    // which the drawing sets by the slot's parity.
    switch (p.subtype)
    {
    case 1:
    case 4:
        p.rotation = F32((static_cast<double>(*worldClock) * p.rotationFactor) * MistTurn + p.start.y);
        break;
    case 2:
    case 5:
        p.rotation = F32((static_cast<double>(*worldClock) * p.rotationFactor) * -MistTurn + p.start.y);
        break;
    default:
        break;
    }
    return {};
}

void StepRisingFlame(Particle& p)
{
    // 0x545e3f: fcomp of the start with x; C0 (less or unordered) turns forward.
    if (!(p.start.x >= p.position.x))
        p.rotation = F32(static_cast<double>(p.rotation) + FlameTurn);
    else
        p.rotation = F32(static_cast<double>(p.rotation) - FlameTurn);
    p.position.z = F32(static_cast<double>(p.position.z) + p.verticalSpeed);
    // 0x545e75..0x545e85: the store is float, the comparison with 0 uses the x87 stack value.
    const double scale = static_cast<double>(p.scale) - static_cast<double>(p.verticalSpeed) * FlameShrink;
    p.scale = F32(scale);
    if (!(scale > 0.0))
        p.alive = false;
    ScaleLight(p, FlameFade);
}

void StepChaseLight(Particle& p)
{
    ScaleLight(p, p.life < ChaseFadeLife ? ChaseFade : ChaseBrighten);
    p.position.z = F32(static_cast<double>(p.verticalSpeed) + p.position.z);
}
} // namespace

bool IsSelectionParticle(std::int32_t type, std::int32_t subtype)
{
    // 0x54327b/0x54747e: 0 to 5 (Icarus's and the selection's 1) and 7 (Chaos Castle's 0xc) share creation (0x5435cf)
    // and step; 7 does not turn (0x547731 sends it to the default).
    constexpr std::int32_t LastOwnedMist = 5;
    constexpr std::int32_t BrightOwnedMist = 7;
    return (type == CloudParticle && ((subtype >= 0 && subtype <= LastOwnedMist) || subtype == BrightOwnedMist)) ||
           (type == FlameParticle && (subtype == RisingFlameSubtype || subtype == FloorFlameSubtype)) ||
           (type == ChaseLightParticle && (subtype == 0 || subtype == 1));
}

void CreateSelectionParticle(Particle& particle, float scaleArgument, Crt::CrtRandom& random)
{
    if (particle.type == CloudParticle)
        CreateOwnedMist(particle, scaleArgument, random);
    else if (particle.type == FlameParticle && particle.subtype == FloorFlameSubtype)
        CreateFloorFlame(particle, scaleArgument, random);
    else if (particle.type == FlameParticle)
        CreateRisingFlame(particle, random);
    else
        CreateChaseLight(particle, random);
}

Core::Expected<void, ParticleError> StepSelectionParticle(Particle& particle, const float* worldClock,
                                                          IParticleTrigonometry* trigonometry,
                                                          const ParticleObjectOwners& owners)
{
    if (particle.type == CloudParticle)
        return StepOwnedMist(particle, worldClock, trigonometry, owners);
    // 0x545d03: 5de/0 has no step beyond the common life and movement.
    if (particle.type == FlameParticle && particle.subtype == RisingFlameSubtype)
        StepRisingFlame(particle);
    else if (particle.type == ChaseLightParticle)
        StepChaseLight(particle);
    return {};
}
} // namespace Client::Particles
