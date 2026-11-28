#include "Particles/MissileFireParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>

namespace Client::Particles
{
namespace
{
constexpr int FullTurn = Math::FullTurn;
constexpr double Hundredth = Math::HundredthAsDouble;                              // 0x6b86c0, 0.01f
constexpr double Tenth = Math::TenthAsDouble;                                      // 0x6b87b8 and 0x6b8a34, 0.1f
constexpr double Fifth = Math::FifthAsDouble;                                      // 0x6b8854, 0.2f
constexpr double Speedup = static_cast<double>(std::bit_cast<float>(0x3f866666u)); // 0x6ba3d4, 1.05f

using Math::F32;

// 0x540d1b..0x540d4f and 0x540d5c..0x540d90: the life and the speed (0; ((rand() & 7) + 32) · step; 0).
void Push(Particle& particle, Crt::CrtRandom& random, std::int32_t life, double step)
{
    constexpr int PushMask = 7, PushBase = 32;
    particle.life = life;
    particle.velocity.x = 0.0f;
    const int push = (random.Next() & PushMask) + PushBase;
    particle.velocity.z = 0.0f;
    particle.velocity.y = F32(static_cast<double>(push) * step);
}

// 0x540dd6..0x540dfb, 0x540e7b..0x540ea5 and 0x540eea..0x540f0f: the rise (rand() % 100 · 0.01f) · 4 plus `base`.
double Lift(Crt::CrtRandom& random)
{
    constexpr int LiftSpan = 100;
    constexpr double LiftStep = 4.0; // 0x6b8a44
    return (static_cast<double>(random.Next() % LiftSpan) * Hundredth) * LiftStep;
}

// 0x540e19..0x540e35 and the following ones: the scale rand() % 6 · step plus the requested one.
void Grow(Particle& particle, Crt::CrtRandom& random, double step)
{
    constexpr int GrowSpan = 6;
    particle.scale = F32(static_cast<double>(random.Next() % GrowSpan) * step + static_cast<double>(particle.scale));
}

// 0x5456ba..0x5456db: the speed by 1.05f.
void Accelerate(Particle& particle)
{
    for (int axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * Speedup);
}

// 0x5456ea..0x5456f9: the gray light life · 0.2f.
void Glow(Particle& particle)
{
    particle.light = glm::vec3(F32(static_cast<double>(particle.life) * Fifth));
}

// 0x5456de..0x5456e7: the z adds the rise times 20.
void Climb(Particle& particle, double speed)
{
    constexpr double ClimbStep = 20.0; // 0x6b86d0
    particle.position.z = F32(speed * ClimbStep + static_cast<double>(particle.position.z));
}

void Rise(Particle& particle)
{
    particle.position.z = F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.position.z));
}

// 0x5455b0..0x5455ec: a = (life + x speed) · 0.1f; the x is the start plus sin(a) · 120, the y the start minus
// cos(a) · 120, the z adds the rise and the scale drops 0.002f.
Core::Expected<void, ParticleError> Orbit(Particle& particle, IParticleTrigonometry& trigonometry)
{
    constexpr float Radius = 120.0f;                                                  // 0x6b86c8
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc, 0.002f
    const double angle = (static_cast<double>(particle.life) + static_cast<double>(particle.velocity.x)) * Tenth;
    const auto sine = trigonometry.SineProductPC53(angle, Radius);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    particle.position.x = F32(sine.Value() + static_cast<double>(particle.start.x));
    const auto cosine = trigonometry.CosineProductPC53(angle, Radius);
    if (!cosine.HasValue())
        return Core::Unexpected(cosine.Error());
    particle.position.y = F32(static_cast<double>(particle.start.y) - cosine.Value());
    Rise(particle);
    particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
    return {};
}

// 0x5455f9..0x54562d: the x adds cos(z angle) · 20 and the y sin(z angle) · 20 (the raw angle, 0x6ba2f8 in double),
// the z the rise and the turn 1.
Core::Expected<void, ParticleError> Swirl(Particle& particle, IParticleTrigonometry& trigonometry)
{
    constexpr float Reach = 20.0f;
    const auto cosine = trigonometry.CosineProductPC53(static_cast<double>(particle.angles.z), Reach);
    if (!cosine.HasValue())
        return Core::Unexpected(cosine.Error());
    particle.position.x = F32(cosine.Value() + static_cast<double>(particle.position.x));
    const auto sine = trigonometry.SineProductPC53(static_cast<double>(particle.angles.z), Reach);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    particle.position.y = F32(sine.Value() + static_cast<double>(particle.position.y));
    Rise(particle);
    particle.rotation = F32(static_cast<double>(particle.rotation) + 1.0);
    return {};
}
} // namespace

bool IsMissileFireKernel(std::int32_t type, std::int32_t subtype)
{
    constexpr std::int32_t MissileSparkSubtype = 8, MissileBlueSubtype = 9;
    return type == MissileFireParticle && subtype != MissileSparkSubtype && subtype != MissileBlueSubtype;
}

void CreateMissileFire(Particle& particle, Crt::CrtRandom& random)
{
    constexpr double Back = static_cast<double>(std::bit_cast<float>(0xbe99999au));        // 0x6ba274, −0.3f
    constexpr double SlowBack = static_cast<double>(std::bit_cast<float>(0xbdcccccdu));    // 0x6b8930, −0.1f
    constexpr double OrbitGrowth = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    constexpr double SwirlTilt = static_cast<double>(std::bit_cast<float>(0x3f99999au));   // 0x6b8bb0, 1.2f
    constexpr double RiseBase = 5.0;                                                       // 0x6b87f8
    constexpr int SpreadSpan = 300, SpreadCenter = 150;
    switch (particle.subtype)
    {
    case MissileTrailSubtype:
        // 0x541045..0x54107c: life 12 and the scale 0.8f.
        Push(particle, random, 12, Back);
        particle.scale = std::bit_cast<float>(0x3f4ccccdu);
        break;
    case MissileBurstSubtype:
        Push(particle, random, 3, Back);
        break;
    case MissileDriftSubtype:
        Push(particle, random, 5, SlowBack);
        break;
    case MissileLongTrailSubtype:
        Push(particle, random, 7, SlowBack);
        break;
    case MissileOrbitSubtype:
    {
        // 0x540dcf..0x540e67: life 100, the rise plus 1, the x speed rand() % 300 − 150 (the lap's phase), the scale,
        // the angles at 0, a turn that the final one overrides and the start at the position.
        particle.life = 100;
        particle.verticalSpeed = F32(Lift(random) + 1.0);
        particle.velocity.x = static_cast<float>(random.Next() % SpreadSpan - SpreadCenter);
        Grow(particle, random, OrbitGrowth);
        const int turn = random.Next() % FullTurn;
        particle.angles = glm::vec3(0.0f);
        particle.rotation = static_cast<float>(turn);
        particle.start = particle.position;
        break;
    }
    case MissileSwirlSubtype:
        // 0x540e74..0x540edd: life 6, the rise plus the x angle · 1.2f, the scale and the turn; the start.
        particle.life = 6;
        particle.verticalSpeed = F32(Lift(random) + static_cast<double>(particle.angles.x) * SwirlTilt);
        Grow(particle, random, Fifth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        particle.start = particle.position;
        break;
    case MissileRiseSubtype:
        // 0x540ee7..0x540f47: life 6, the rise plus 5, the scale and the turn; the start.
        particle.life = 6;
        particle.verticalSpeed = F32(Lift(random) + RiseBase);
        Grow(particle, random, Tenth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        particle.start = particle.position;
        break;
    case MissileFadeSubtype:
        particle.life = 10;
        break;
    default:
        break;
    }
    // 0x54107f: the turn rand() % 360, for all.
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
}

Core::Expected<void, ParticleError> StepMissileFire(Particle& particle, IParticleTrigonometry* trigonometry)
{
    constexpr double Lift = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b8b90, 0.02f
    constexpr double Turn = 4.0;                                                    // 0x6b8a44
    switch (particle.subtype)
    {
    case MissileTrailSubtype:
    case MissileLongTrailSubtype:
    {
        // 0x5456a6..0x5456e7: the rise adds 0.02f and the scale adds the unrounded sum; the speed by 1.05f and the z
        // the rise times 20.
        const double speed = static_cast<double>(particle.verticalSpeed) + Lift;
        particle.verticalSpeed = F32(speed);
        particle.scale = F32(speed + static_cast<double>(particle.scale));
        Accelerate(particle);
        Climb(particle, speed);
        Glow(particle);
        return {};
    }
    case MissileBurstSubtype:
        // 0x54556d..0x545594.
        Accelerate(particle);
        Climb(particle, static_cast<double>(particle.verticalSpeed));
        Glow(particle);
        return {};
    case MissileDriftSubtype:
        Glow(particle);
        return {};
    case MissileOrbitSubtype:
    case MissileSwirlSubtype:
        if (trigonometry == nullptr)
            return Core::Unexpected(ParticleError::MissingClock);
        return particle.subtype == MissileOrbitSubtype ? Orbit(particle, *trigonometry)
                                                       : Swirl(particle, *trigonometry);
    case MissileRiseSubtype:
        // 0x54563a..0x54564c.
        Rise(particle);
        particle.rotation = F32(static_cast<double>(particle.rotation) + Turn);
        return {};
    case MissileFadeSubtype:
        // 0x545659..0x54568c.
        Accelerate(particle);
        Glow(particle);
        return {};
    default:
        return {};
    }
}

} // namespace Client::Particles
