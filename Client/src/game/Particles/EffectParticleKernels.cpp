#include "Particles/EffectParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/NoriaParticleKernels.h"
#include "Particles/Particles.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/SmokeParticleKernels.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Particles
{
namespace
{
constexpr int FullTurn = Math::FullTurn;
constexpr double Hundredth = Math::HundredthAsDouble;                                 // 0x6b88d4 and 0x6b86c0, 0.01f
constexpr double Half = Math::HalfAsDouble;                                           // 0x6b882c
constexpr double Tenth = Math::TenthAsDouble;                                         // 0x6b87b8 and 0x6b8a34, 0.1f
constexpr double Fifth = Math::FifthAsDouble;                                         // 0x6b8cfc and 0x6b8854, 0.2f
constexpr double FourFifths = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0.8f
constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));      // 0x6b87b4, 0.4f
constexpr double Twentieth = Math::TwentiethAsDouble;                                 // 0x6b8a48, 0.05f
constexpr double Countdown = static_cast<double>(std::bit_cast<float>(0x3a83126fu));  // 0x6b89c8, 0.001f

using Math::F32;

void ScaleLight(Particle& particle, double factor)
{
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * factor);
}

void SubtractLight(Particle& particle, double amount)
{
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = F32(static_cast<double>(particle.light[channel]) - amount);
}

// 0x4e98d0: the position plus the speed rotated by AngleMatrix of the angles, each sum in 53 bits.
void Move(Particle& particle)
{
    const glm::vec3 turned = Math::OriginalVectorRotate(particle.velocity, Math::OriginalAngleMatrix(particle.angles));
    for (int axis = 0; axis < 3; ++axis)
        particle.position[axis] = F32(static_cast<double>(turned[axis]) + static_cast<double>(particle.position[axis]));
}

bool IsSmoke(std::int32_t type, std::int32_t subtype)
{
    return type == BreathParticle && (subtype == PoisonSmokeSubtype || subtype == EffectSmokeSubtype);
}

bool IsEffectFlame(std::int32_t type, std::int32_t subtype)
{
    return type == FlameParticle && subtype != FloorFlameSubtype && subtype != RisingFlameSubtype;
}

// The 5c6 that the 0x5443c0 table leaves in the prefix or in cases without their own kernel. 1, 2, 5, 9, 11..13 and
// 15 have theirs in Particles.cpp and WeaponSparkKernels.
bool IsEffectSpark(std::int32_t type, std::int32_t subtype)
{
    if (type != SparkParticle)
        return false;
    switch (subtype)
    {
    case 1:
    case 2:
    case 5:
    case 9:
    case 11:
    case 12:
    case 13:
    case 15:
        return false;
    default:
        return true;
    }
}

// 0x541722..0x541748, 0x54178c..0x5417b2 and 0x5417f3..: the clock light, L = sin(clock · 0.002f) · 0.3f + 0.7f: red
// L, green and blue L · 0.5, each from the unrounded sum.
Core::Expected<void, ParticleError> LightTheFlameByTheClock(Particle& particle, const EffectParticleBirth& birth)
{
    constexpr double ClockStep = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc, 0.002f
    constexpr float Swing = std::bit_cast<float>(0x3e99999au);                           // 0x6b89dc, 0.3f
    constexpr double Base = static_cast<double>(std::bit_cast<float>(0x3f333333u));      // 0x6b8a5c, 0.7f
    if (birth.worldClock == nullptr || birth.trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    const auto sine = birth.trigonometry->SineProductPC53(static_cast<double>(*birth.worldClock) * ClockStep, Swing);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    const double glow = sine.Value() + Base;
    particle.light.x = F32(glow);
    particle.light.y = F32(glow * Half);
    particle.light.z = F32(glow * Half);
    return {};
}

// 0x541eb6..0x541fa2 (/1) and 0x541cf2..0x541d80 (/3): the scale ((rand() & 31) + 80) · 0.01f, the /1 shifts the
// position ((rand() & 63) − 32; (rand() & 63) − 32; (rand() & 63) + 32), the x angle rand() % 90 − 45, the z rand() %
// 360 and the speed (0; −((rand() & 7) + 40); 0). The /1 lives 50 with seven rand(); the /3, 10 with four.
void CreateTheSmoke(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t PoisonLife = 50, EffectLife = 10;
    constexpr int ScaleMask = 31, ScaleBase = 80, OffsetMask = 63, OffsetCenter = 32, TiltSpan = 90, TiltCenter = 45;
    constexpr int SpeedMask = 7, SpeedBase = 40;
    const bool poison = particle.subtype == PoisonSmokeSubtype;
    particle.life = poison ? PoisonLife : EffectLife;
    particle.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * Hundredth);
    if (poison)
    {
        particle.position.x =
            F32(static_cast<double>((random.Next() & OffsetMask) - OffsetCenter) + particle.position.x);
        particle.position.y =
            F32(static_cast<double>((random.Next() & OffsetMask) - OffsetCenter) + particle.position.y);
        particle.position.z =
            F32(static_cast<double>((random.Next() & OffsetMask) + OffsetCenter) + particle.position.z);
    }
    particle.angles.x = static_cast<float>(random.Next() % TiltSpan - TiltCenter);
    particle.velocity.x = 0.0f;
    particle.angles.z = static_cast<float>(random.Next() % FullTurn);
    const int speed = (random.Next() & SpeedMask) + SpeedBase;
    particle.velocity.z = 0.0f;
    particle.velocity.y = static_cast<float>(-speed);
}

// The 0x54448c table, cases 1..6 (0x541603, 0x541693, 0x541750, 0x5417ba, 0x54166a and 0x5414a5). Subtypes outside
// the table only have the prefix.
Core::Expected<void, ParticleError> CreateTheFlame(Particle& particle, Crt::CrtRandom& random,
                                                   const EffectParticleBirth& birth)
{
    constexpr int GrowthMask = 31, GrowthBase = 32, RiseMask = 3, RiseBase = 4, TossSpan = 80;
    constexpr double RiseStep = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    constexpr double TossBack = static_cast<double>(std::bit_cast<float>(0x3d888889u)); // 0x6ba1e0, 0.0666…f
    constexpr double JetStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));  // 0x6ba1d8, 0.12f
    constexpr int JetMask = 127, JetBase = 256, JetScaleMask = 63, JetScaleBase = 64;
    constexpr float FollowRise = 10.0f; // 0x5417d5, 0x41200000
    const auto grow = [&]
    {
        particle.scale = F32(static_cast<double>((random.Next() & GrowthMask) + GrowthBase) * Hundredth +
                             static_cast<double>(particle.scale));
    };
    switch (particle.subtype)
    {
    case 1:
    case 5:
        // 0x541603 (/1: life 15 and the scale grows) and 0x54166a (/5: life 4): the speed (0; ((rand() & 3) + 4) ·
        // 0.15f; 0).
        particle.life = particle.subtype == 1 ? 15 : 4;
        if (particle.subtype == 1)
        {
            const int growth = (random.Next() & GrowthMask) + GrowthBase;
            particle.velocity.x = 0.0f;
            particle.scale = F32(static_cast<double>(growth) * Hundredth + static_cast<double>(particle.scale));
        }
        else
            particle.velocity.x = 0.0f;
        {
            const int rise = (random.Next() & RiseMask) + RiseBase;
            particle.velocity.z = 0.0f;
            particle.velocity.y = F32(static_cast<double>(rise) * RiseStep);
        }
        return {};
    case 2:
    {
        // 0x541693..0x54174b: life 10, the scale grows, a jump t = rand() % 80 · red · 0.01f forward with 0x4e98d0 and
        // the speed (0; (red − t) · 0.0666667f; 0). Then the clock light.
        particle.life = 10;
        grow();
        const int toss = random.Next() % TossSpan;
        const float red = particle.light.x;
        const float jump = F32(static_cast<double>(toss) * static_cast<double>(red) * Hundredth);
        particle.velocity.x = 0.0f;
        particle.velocity.z = 0.0f;
        particle.velocity.y = jump;
        Move(particle);
        particle.velocity.x = 0.0f;
        particle.velocity.z = 0.0f;
        particle.velocity.y = F32((static_cast<double>(red) - static_cast<double>(jump)) * TossBack);
        return LightTheFlameByTheClock(particle, birth);
    }
    case 3:
        // 0x541750..0x5417b2: life 10, the scale grows, still and with the clock light.
        particle.life = 10;
        grow();
        particle.velocity = glm::vec3(0.0f);
        return LightTheFlameByTheClock(particle, birth);
    case FollowingFlameSubtype:
        // 0x5417ba..: life 5, the rise 10, the scale grows, the start at the owner's position and the clock light.
        particle.life = 5;
        grow();
        particle.verticalSpeed = FollowRise;
        if (birth.ownerPosition == nullptr)
            return Core::Unexpected(ParticleError::MissingOwner);
        particle.start = *birth.ownerPosition;
        return LightTheFlameByTheClock(particle, birth);
    case 6:
    {
        // 0x5414a5..0x541506: life 10, rises straight ((rand() & 127) + 256) · 0.12f and the scale ((rand() & 63) + 64)
        // · the requested one · 0.01f.
        particle.life = 10;
        particle.velocity.x = 0.0f;
        particle.velocity.y = 0.0f;
        particle.velocity.z = F32(static_cast<double>((random.Next() & JetMask) + JetBase) * JetStep);
        const float argumentScale = particle.scale;
        particle.scale =
            F32(static_cast<double>((random.Next() & JetScaleMask) + JetScaleBase) * argumentScale * Hundredth);
        return {};
    }
    default:
        return {};
    }
}

// The 0x5443c0 table for the sparks without their own kernel (subtype − 1; the /0, the /14 and those outside 1..15
// only have the prefix).
Core::Expected<void, ParticleError> CreateTheSpark(Particle& particle, Crt::CrtRandom& random,
                                                   const EffectParticleBirth& birth)
{
    constexpr double Approach = static_cast<double>(std::bit_cast<float>(0x3e2aaaabu)); // 0x6ba05c, 1/6
    constexpr int RareChance = 50, RareLife = 50, FallSpan = 20, ScaleSpan = 5, JitterSpan = 60, JitterCenter = 30;
    constexpr int SideMask = 7, SideCenter = 4;
    constexpr float GreenRise = std::bit_cast<float>(0x3fa66666u); // 0x540809, 1.3f
    const auto spread = [&] { particle.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * Tenth + 1.0); };
    switch (particle.subtype)
    {
    case ShrinkingSparkSubtype:
        particle.life = 5; // 0x540679
        return {};
    case FollowingSparkSubtype:
        // 0x540686..0x5406dd: no angles, life 10, +0x38 at 0.1f, the start at the position and the speed toward the
        // owner, a sixth of the distance per step.
        if (birth.ownerPosition == nullptr)
            return Core::Unexpected(ParticleError::MissingOwner);
        particle.angles = glm::vec3(0.0f);
        particle.start = particle.position;
        particle.life = 10;
        particle.field38 = 0x3dcccccdu;
        for (int axis = 0; axis < 3; ++axis)
            particle.velocity[axis] =
                F32((static_cast<double>((*birth.ownerPosition)[axis]) - static_cast<double>(particle.position[axis])) *
                    Approach);
        return {};
    case RareSparkSubtype:
        // 0x54073d..0x5407af: with rand() % 50 nonzero, life 0 (the step turns it off); otherwise life 50, the fall
        // (rand() % 20) · 0.2f + 1 and the scale (rand() % 5) · 0.1f + 1.
        if (random.Next() % RareChance != 0)
        {
            particle.life = 0;
            return {};
        }
        particle.life = RareLife;
        particle.verticalSpeed = F32(static_cast<double>(random.Next() % FallSpan) * Fifth + 1.0);
        spread();
        return {};
    case GreenSparkSubtype:
    {
        // 0x5407b0..0x540849: +0x38 at 1.0f, y and z shifted rand() % 60 − 30 (added before subtracting), life 30, the
        // fall 1.3f, the x angle shifted the same way and the scale (rand() % 5) · 0.1f + 1.
        particle.field38 = std::bit_cast<std::uint32_t>(1.0f);
        particle.position.y =
            F32((static_cast<double>(random.Next() % JitterSpan) + particle.position.y) - JitterCenter);
        particle.position.z =
            F32((static_cast<double>(random.Next() % JitterSpan) + particle.position.z) - JitterCenter);
        const int tilt = random.Next() % JitterSpan;
        particle.life = 30;
        particle.verticalSpeed = GreenRise;
        particle.angles.x = F32((static_cast<double>(tilt) + particle.angles.x) - JitterCenter);
        spread();
        return {};
    }
    case ShortHitSparkSubtype:
        particle.life = 10; // 0x540100
        return {};
    case BurstSparkSubtype:
    {
        // 0x5408f7..0x540972: +0x38 at 1.0f, the speed ((rand() & 7) − 4; 0; (rand() & 7) − 4), life 20 and the scale
        // (rand() % 5) · 0.1f + 1.
        particle.field38 = std::bit_cast<std::uint32_t>(1.0f);
        const int side = random.Next() & SideMask;
        particle.velocity.y = 0.0f;
        particle.velocity.x = static_cast<float>(side - SideCenter);
        const int up = random.Next() & SideMask;
        particle.life = 20;
        particle.velocity.z = static_cast<float>(up - SideCenter);
        spread();
        return {};
    }
    default:
        return {};
    }
}

// 0x5473b1..0x547468: the /0 beats with the life (scale sin(life · 0.17453292f) · 3 + 2, light life · 0.2f); the /1
// grows with +0x50 (scale sin(+0x50 · π/180) · 3 + 1.5; +0x50 adds (15 − life) · 6 capped at 90) and with life under
// 6 dims the light by half. The other subtypes do nothing.
Core::Expected<void, ParticleError> StepTheRing(Particle& particle, IParticleTrigonometry* trigonometry)
{
    constexpr double PulseStep = static_cast<double>(std::bit_cast<float>(0x3e32b8c2u)); // 0x6ba370
    constexpr double Degree = Math::DegreesToRadiansAsDouble;                            // 0x6ba360
    constexpr float Swing = 3.0f;                                                        // 0x6b86b8
    constexpr double PulseBase = 2.0;                                                    // 0x6b8818
    constexpr double BurstBase = 1.5;                                                    // 0x6b8b94
    constexpr double BurstStep = 6.0;                                                    // 0x6b87f4
    constexpr double BurstLimit = 90.0;                                                  // 0x6b8948
    constexpr std::int32_t BurstLife = 15, DimLife = 6;
    if (particle.subtype != RingPulseSubtype && particle.subtype != RingBurstSubtype)
        return {};
    if (trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    if (particle.subtype == RingPulseSubtype)
    {
        const double life = static_cast<double>(particle.life);
        const auto sine = trigonometry->SineProductPC53(life * PulseStep, Swing);
        if (!sine.HasValue())
            return Core::Unexpected(sine.Error());
        particle.scale = F32(sine.Value() + PulseBase);
        particle.light = glm::vec3(F32(life * Fifth));
        return {};
    }
    const auto sine = trigonometry->SineProductPC53(static_cast<double>(particle.verticalSpeed) * Degree, Swing);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    particle.scale = F32(sine.Value() + BurstBase);
    const double sum =
        static_cast<double>(BurstLife - particle.life) * BurstStep + static_cast<double>(particle.verticalSpeed);
    particle.verticalSpeed = F32(sum);
    // 0x547427: fcomp with test ah,0x41; the cap only when the unrounded sum is greater.
    if (sum > BurstLimit)
        particle.verticalSpeed = static_cast<float>(BurstLimit);
    if (particle.life < DimLife)
        ScaleLight(particle, Half);
    return {};
}

// 0x546250..0x5462a0 (/1) and 0x5460c2..0x54610c (/3): the light with the life, the speed by 0.4f and the scale grows.
void StepTheSmoke(Particle& particle)
{
    constexpr double PoisonFade = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b89e4, 0.02f
    constexpr double EffectFade = 0.125;                                                  // 0x6ba170
    if (particle.subtype == PoisonSmokeSubtype)
    {
        const double fade = static_cast<double>(particle.life) * PoisonFade;
        particle.light = glm::vec3(F32(fade * Half), F32(fade), F32(fade * FourFifths));
    }
    else
    {
        const double fade = static_cast<double>(particle.life) * EffectFade;
        const float gray = F32(fade * FourFifths);
        particle.light = glm::vec3(gray, gray, F32(fade));
    }
    for (int axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * Brake);
    particle.scale =
        F32(static_cast<double>(particle.scale) + (particle.subtype == PoisonSmokeSubtype ? Twentieth : Tenth));
}

// The 0x5482f4 table (0x545d03..0x545e3f), after its own death with life 0 or less.
Core::Expected<void, ParticleError> StepTheFlame(Particle& particle, Crt::CrtRandom& random,
                                                 const ParticleOwnerStates& ownerStates)
{
    constexpr std::int32_t KickLife = 10;
    constexpr double Kick = static_cast<double>(std::bit_cast<float>(0x40cccccdu));   // 0x6ba3bc, 6.4f
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    if (particle.life <= 0)
        particle.alive = false;
    const auto turn = [&] { particle.rotation = static_cast<float>(random.Next() % FullTurn); };
    switch (particle.subtype)
    {
    case 1:
    case 5:
        if (particle.life == KickLife)
        {
            particle.velocity.y = F32(static_cast<double>(particle.velocity.y) + Kick);
            particle.scale = particle.subtype == 1 ? F32(static_cast<double>(particle.scale) - Shrink)
                                                   : F32(static_cast<double>(particle.scale) * FourFifths);
        }
        turn();
        SubtractLight(particle, Twentieth);
        return {};
    case 2:
        turn();
        SubtractLight(particle, Twentieth);
        return {};
    case 3:
        turn();
        return {};
    case FollowingFlameSubtype:
    {
        // 0x545ddf..0x545e3a: the position follows what the owner moved since the start (z also adds +0x50), the start
        // becomes the owner's position and +0x50 adds 0.1f.
        const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
        if (!owner)
            return Core::Unexpected(ParticleError::MissingOwner);
        particle.position.x = F32((static_cast<double>(owner->position.x) - particle.start.x) + particle.position.x);
        particle.position.y = F32((static_cast<double>(owner->position.y) - particle.start.y) + particle.position.y);
        particle.position.z = F32(((static_cast<double>(owner->position.z) - particle.start.z) + particle.position.z) +
                                  particle.verticalSpeed);
        particle.start = owner->position;
        particle.verticalSpeed = F32(static_cast<double>(particle.verticalSpeed) + Tenth);
        turn();
        SubtractLight(particle, Twentieth);
        return {};
    }
    default:
        return {};
    }
}

// +0x38 drops 0.001f; it dies when the unrounded subtraction is not greater than 0 (fst and fcomp with test ah,0x41).
void CountDown(Particle& particle)
{
    const double left = static_cast<double>(std::bit_cast<float>(particle.field38)) - Countdown;
    particle.field38 = std::bit_cast<std::uint32_t>(F32(left));
    if (!(left > 0.0))
        particle.alive = false;
}

// The 0x548204 table for the sparks without their own kernel.
Core::Expected<void, ParticleError> StepTheSpark(Particle& particle, Crt::CrtRandom& random,
                                                 const ParticleOwnerStates& ownerStates)
{
    constexpr double HitShrink = 0.5;                                                       // 0x6b882c
    constexpr double HitMinimum = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));   // 0x6b8854, 0.2f
    constexpr double FollowGrowth = static_cast<double>(std::bit_cast<float>(0x3da3d70au)); // 0x6ba1ac, 0.08f
    constexpr double GreenShrink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));  // 0x6b8b90, 0.02f
    constexpr double BurstFade = static_cast<double>(std::bit_cast<float>(0x3f6d097bu));    // 0x6ba150
    constexpr double BurstShrink = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));  // 0x6b8bd4, 0.03f
    constexpr double RareGreen = static_cast<double>(std::bit_cast<float>(0x3f333333u));    // 0x6b8a5c, 0.7f
    constexpr std::int32_t DetachedLife = 5;
    constexpr int RareSpan = 10, GreenSpan = 50;
    switch (particle.subtype)
    {
    case HitSparkSubtype:
    case ShortHitSparkSubtype:
    {
        // 0x545084: the scale drops 0.5 and it dies under 0.2f (or unordered), comparing the unrounded subtraction.
        const double shrunk = static_cast<double>(particle.scale) - HitShrink;
        particle.scale = F32(shrunk);
        if (!(shrunk >= HitMinimum))
            particle.alive = false;
        return {};
    }
    case ShrinkingSparkSubtype:
        particle.scale = F32(static_cast<double>(particle.scale) * FourFifths); // 0x54513f
        return {};
    case FollowingSparkSubtype:
    {
        // 0x545150..0x545204: with the owner dead it goes to life 5; the light life · 0.2f, the scale + 0.08f, the
        // position (and the start) at start + speed, and then what the owner moved since its start (+0x17c): the x with
        // the unrounded subtraction, the y and the z rounded before adding.
        if (particle.life <= 0)
            particle.alive = false;
        const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
        if (!owner || !owner->start)
            return Core::Unexpected(ParticleError::MissingOwner);
        if (particle.life > DetachedLife && !owner->alive)
            particle.life = DetachedLife;
        particle.light = glm::vec3(F32(static_cast<double>(particle.life) * Fifth));
        particle.scale = F32(static_cast<double>(particle.scale) + FollowGrowth);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float moved =
                F32(static_cast<double>(particle.velocity[axis]) + static_cast<double>(particle.start[axis]));
            particle.position[axis] = moved;
            particle.start[axis] = moved;
        }
        const double dx = static_cast<double>(owner->position.x) - owner->start->x;
        const float dy = F32(static_cast<double>(owner->position.y) - owner->start->y);
        const float dz = F32(static_cast<double>(owner->position.z) - owner->start->z);
        particle.position.x = F32(dx + particle.position.x);
        particle.position.y = F32(static_cast<double>(dy) + particle.position.y);
        particle.position.z = F32(static_cast<double>(dz) + particle.position.z);
        return {};
    }
    case RareSparkSubtype:
        // 0x54521a..0x545267: falls with +0x50, shifts −1 in y, the fall grows 0.1f and the green is (rand() % 10) ·
        // 0.01f + 0.7f.
        if (particle.life <= 0)
            particle.alive = false;
        particle.position.z = F32(static_cast<double>(particle.position.z) - particle.verticalSpeed);
        particle.position.y = F32(static_cast<double>(particle.position.y) - 1.0);
        particle.verticalSpeed = F32(static_cast<double>(particle.verticalSpeed) + Tenth);
        particle.light.y = F32(static_cast<double>(random.Next() % RareSpan) * Hundredth + RareGreen);
        return {};
    case GreenSparkSubtype:
    {
        // 0x54526c..0x5452c4: the scale drops 0.02f, +0x38 counts down, it falls with +0x50 and the light is (0;
        // (rand() % 50) · 0.01f + 0.2f; 0).
        particle.scale = F32(static_cast<double>(particle.scale) - GreenShrink);
        CountDown(particle);
        particle.position.z = F32(static_cast<double>(particle.position.z) - particle.verticalSpeed);
        const int green = random.Next() % GreenSpan;
        particle.light.x = 0.0f;
        particle.light.z = 0.0f;
        particle.light.y = F32(static_cast<double>(green) * Hundredth + Fifth);
        return {};
    }
    case BurstSparkSubtype:
        // 0x545313..0x545363: the light by 0.9259259f, the scale drops 0.03f and +0x38 counts down.
        ScaleLight(particle, BurstFade);
        particle.scale = F32(static_cast<double>(particle.scale) - BurstShrink);
        CountDown(particle);
        return {};
    default:
        // The /14 and those outside 0..15 (0x5481df).
        return {};
    }
}
} // namespace

bool IsEffectParticle(std::int32_t type, std::int32_t subtype)
{
    return type == RingParticle || IsSmoke(type, subtype) || IsSmokeParticle(type, subtype) ||
           IsEffectFlame(type, subtype) || IsEffectSpark(type, subtype);
}

bool FollowsItsOwner(std::int32_t type, std::int32_t subtype)
{
    return (type == SparkParticle && subtype == FollowingSparkSubtype) ||
           (type == LightningParticle && subtype == LightningFollowSubtype) ||
           ((type == FireParticle || type == DeathSpiritFireParticle || type == BlueTrailFireParticle) &&
            subtype == FireRingSubtype) ||
           (type == FlameParticle && subtype == FollowingFlameSubtype) ||
           (type == BreathParticle && subtype == TrailSmokeSubtype) || IsOwnedFairySpark(type, subtype) ||
           IsOwnerBirthFairySpark(type, subtype);
}

bool NeedsOwnerPosition(std::int32_t type, std::int32_t subtype)
{
    return (type == SparkParticle && subtype == FollowingSparkSubtype) ||
           (type == FlameParticle && subtype == FollowingFlameSubtype) || IsOwnerBirthFairySpark(type, subtype);
}

bool NeedsTerrainHeight(std::int32_t type, std::int32_t subtype)
{
    return IsStormMist(type, subtype);
}

Core::Expected<void, ParticleError> CreateEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                         const EffectParticleBirth& birth)
{
    if (particle.type == RingParticle)
    {
        // 0x543183..0x5431b9: life 20; the /1, life 15, +0x50 at 0 and the turn rand() % 360.
        particle.life = 20;
        if (particle.subtype == RingBurstSubtype)
        {
            particle.life = 15;
            particle.verticalSpeed = 0.0f;
            particle.rotation = static_cast<float>(random.Next() % FullTurn);
        }
        return {};
    }
    if (IsStormMist(particle.type, particle.subtype))
    {
        if (birth.terrainHeight == nullptr || !*birth.terrainHeight)
            return Core::Unexpected(ParticleError::MissingTerrainHeight);
        return CreateStormMist(particle, random, *birth.terrainHeight, birth.worldClock, birth.trigonometry);
    }
    if (IsSmokeParticle(particle.type, particle.subtype))
    {
        CreateSmokeParticle(particle, random, birth.worldClock != nullptr ? *birth.worldClock : birth.legacyClock);
        return {};
    }
    if (particle.type == BreathParticle)
    {
        CreateTheSmoke(particle, random);
        return {};
    }
    if (particle.type == FlameParticle)
        return CreateTheFlame(particle, random, birth);
    return CreateTheSpark(particle, random, birth);
}

Core::Expected<void, ParticleError> StepEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                       IParticleTrigonometry* trigonometry,
                                                       const ParticleOwnerStates& ownerStates)
{
    if (particle.type == RingParticle)
        return StepTheRing(particle, trigonometry);
    if (IsSmokeParticle(particle.type, particle.subtype))
        return StepSmokeParticle(particle, random, ownerStates);
    if (particle.type == BreathParticle)
    {
        StepTheSmoke(particle);
        return {};
    }
    if (particle.type == FlameParticle)
        return StepTheFlame(particle, random, ownerStates);
    return StepTheSpark(particle, random, ownerStates);
}

} // namespace Client::Particles
