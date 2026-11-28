#include "Particles/SmokeParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/Ftol.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Particles
{
namespace
{
constexpr int FullTurn = Math::FullTurn;
constexpr double Hundredth = Math::HundredthAsDouble;                                 // 0x6b88d4 and 0x6b86c0
constexpr double Tenth = Math::TenthAsDouble;                                         // 0x6b87b8
constexpr double Fifth = Math::FifthAsDouble;                                         // 0x6b8854
constexpr double Twentieth = Math::TwentiethAsDouble;                                 // 0x6b8a48 and 0x6b8ad0
constexpr double FourFifths = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38
constexpr double Eighth = Math::EighthAsDouble;                                       // 0x6ba170
constexpr double Half = Math::HalfAsDouble;                                           // 0x6b882c
constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3da3d70au));     // 0x6ba1ac, 0.08f

using Math::F32;

void ScaleLight(Particle& particle, double factor)
{
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * factor);
}

// The requested light stays in +0x60..+0x68 (0x5418b1, 0x54228c).
void KeepTheLight(Particle& particle)
{
    particle.rotationFactor = particle.light.x;
    particle.field64 = {particle.light.y, particle.light.z};
}

// 0x5418ca..0x541924 and 0x5422a5..0x5422ff
void ShakeAtBirth(Particle& particle, Crt::CrtRandom& random, float clock)
{
    constexpr int ScaleMask = 31, ScaleBase = 8;
    const double argumentScale = static_cast<double>(particle.scale);
    particle.scale = F32((static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * argumentScale) * Hundredth);
    particle.angles.x = static_cast<float>(random.Next() % FullTurn);
    particle.rotation = static_cast<float>(Math::FtolLow(static_cast<double>(clock)) % FullTurn);
}

// 0x541c58..0x541c7d, 0x5424ff..0x542524 and 0x5426f2..0x542717
void RiseAtBirth(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int RiseMask = 31, RiseBase = 60;
    particle.verticalSpeed = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * Tenth);
}

// 0x541981..0x541a00: /36 uses the start as its distance to its owner.
void TrailAtBirth(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t TrailLife = 25;
    constexpr int Spread = 50, Center = 25;
    particle.life = TrailLife;
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    particle.angles.x = static_cast<float>(random.Next() % FullTurn);
    particle.start.x = static_cast<float>(random.Next() % Spread - Center);
    particle.start.y = static_cast<float>(random.Next() % Spread - Center);
    particle.start.z = -static_cast<float>(random.Next() % Spread);
}

// 0x545ed6..0x545ef1: the z adds the vertical speed unrounded.
void RiseLikeTheBreath(Particle& particle, double rise)
{
    const double speed = static_cast<double>(particle.verticalSpeed) + rise;
    particle.verticalSpeed = F32(speed);
    particle.position.z = F32(speed + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + Twentieth);
}

// 0x54658b..0x546616 (/19) and 0x546630..0x5466bb (/20)
void Shake(Particle& particle, Crt::CrtRandom& random)
{
    constexpr double RiseStep = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6ba3a4, 0.002f
    constexpr double GrowStep = static_cast<double>(std::bit_cast<float>(0x3a83126fu)); // 0x6ba128, 0.001f
    constexpr int RiseSpan = 20, DriftSpan = 100, DriftBase = 20, GrowSpan = 10;
    particle.verticalSpeed =
        F32(static_cast<double>(random.Next() % RiseSpan) * RiseStep + static_cast<double>(particle.verticalSpeed));
    particle.position.x = F32(static_cast<double>(random.Next() % DriftSpan - DriftBase) * Hundredth +
                              static_cast<double>(particle.position.x));
    particle.position.y = F32(static_cast<double>(random.Next() % DriftSpan - DriftBase) * Hundredth +
                              static_cast<double>(particle.position.y));
    particle.position.z = F32(static_cast<double>(particle.position.z) + static_cast<double>(particle.verticalSpeed));
    const double grown = static_cast<double>(random.Next() % GrowSpan) * GrowStep + static_cast<double>(particle.scale);
    particle.scale = F32(grown);
    particle.rotation = F32(grown);
}

// 0x545ef9..0x545f63: the z and the y add the vertical speed unrounded; the owner's position is +0x40 → +0x10.
Core::Expected<void, ParticleError> StepTheTrail(Particle& particle, const ParticleOwnerStates& ownerStates)
{
    constexpr std::int32_t FollowLife = 23;
    if (particle.life < FollowLife)
    {
        const double speed = static_cast<double>(particle.scale) * Tenth + static_cast<double>(particle.verticalSpeed);
        particle.verticalSpeed = F32(speed);
        particle.position.z = F32(speed + static_cast<double>(particle.position.z));
        particle.position.y = F32(speed + static_cast<double>(particle.position.y));
        particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
    }
    else
    {
        const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
        if (!owner)
            return Core::Unexpected(ParticleError::MissingOwner);
        for (int axis = 0; axis < 3; ++axis)
            particle.position[axis] =
                F32(static_cast<double>(owner->position[axis]) + static_cast<double>(particle.start[axis]));
    }
    particle.rotation = F32(static_cast<double>(particle.rotation) + Hundredth);
    return {};
}

// 0x545f68..0x545fbf: the z and the y move back along the kept direction (+0x68 and +0x64); the light fades by
// 0.9708738f.
void StepTheBackTrail(Particle& particle)
{
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f788b30u)); // 0x6ba3b0
    particle.position.z =
        F32(static_cast<double>(particle.position.z) - static_cast<double>(particle.field64.y) * FourFifths);
    particle.position.y =
        F32(static_cast<double>(particle.position.y) - static_cast<double>(particle.field64.x) * FourFifths);
    particle.scale = F32(static_cast<double>(particle.scale) + Shrink);
    particle.rotation = F32(static_cast<double>(particle.rotation) + Hundredth);
    ScaleLight(particle, Fade);
}

// 0x546905..0x546952: the light fades by 0.9090909f; with the unrounded scale at 0 or less and the stored blue at 0 or
// less, it goes off (0x545360).
void StepTheShrinking(Particle& particle)
{
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)); // 0x6ba26c
    const double shrunk = static_cast<double>(particle.scale) - Twentieth;
    particle.scale = F32(shrunk);
    ScaleLight(particle, Fade);
    if (!(shrunk > 0.0) && !(particle.light.z > 0.0f))
        particle.alive = false;
}
// 0x542943..0x542a5f: the other subtypes leave through 0x54304c with the prefix.
void CreateThePuff(Particle& particle, Crt::CrtRandom& random, float clock)
{
    constexpr std::int32_t LongLife = 55, ShortLife = 30, ShortSubtype = 1, LastSubtype = 2;
    constexpr int TiltMask = 3;
    constexpr double TiltCenter = 2.0; // 0x6b8818
    if (particle.subtype < 0 || particle.subtype > LastSubtype)
        return;
    KeepTheLight(particle);
    particle.life = particle.subtype == ShortSubtype ? ShortLife : LongLife;
    particle.angles.x = F32(static_cast<double>(random.Next() & TiltMask) - TiltCenter);
    particle.rotation = static_cast<float>(Math::FtolLow(static_cast<double>(clock)) % FullTurn);
}

// 0x546a13..0x546aff: /0 lowers the kept red 0.015f while it is above 0.1f and another 0.005f while the green is above
// 0.1f. The light is life · 0.0181818f times the kept one, and the z adds the rise unrounded.
void StepThePuff(Particle& particle)
{
    constexpr float Floor = std::bit_cast<float>(0x3dcccccdu);                           // 0x6b87b8, 0.1f
    constexpr double Drop = static_cast<double>(std::bit_cast<float>(0x3c75c28fu));      // 0x6b8a24, 0.015f
    constexpr double SmallDrop = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8, 0.005f
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3c94f209u));      // 0x6ba374
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));    // 0x6b8bd4, 0.03f
    float& red = particle.rotationFactor;
    float& green = particle.field64.x;
    float& blue = particle.field64.y;
    double rise = Fifth;
    switch (particle.subtype)
    {
    case 0:
        if (red > Floor)
            red = F32(static_cast<double>(red) - Drop);
        if (green > Floor)
            red = F32(static_cast<double>(red) - SmallDrop);
        break;
    case 1:
        rise = Tenth;
        break;
    case 2:
        if (green > Floor)
            green = F32(static_cast<double>(green) - SmallDrop);
        if (blue > Floor)
            blue = F32(static_cast<double>(blue) - Drop);
        break;
    default:
        return;
    }
    const double fade = static_cast<double>(particle.life) * Fade;
    particle.light = glm::vec3(F32(fade * static_cast<double>(red)), F32(fade * static_cast<double>(green)),
                               F32(fade * static_cast<double>(blue)));
    const double speed = static_cast<double>(particle.verticalSpeed) + rise;
    particle.verticalSpeed = F32(speed);
    particle.position.z = F32(speed + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + Growth);
    particle.rotation = F32(static_cast<double>(particle.angles.x) + static_cast<double>(particle.rotation));
}
// 0x546852..0x546899 (/28) and 0x54689e..0x5468d9 (/29): /29's light at 1 is the ecx of 0x54486e.
void StepTheStormMist(Particle& particle)
{
    constexpr std::int32_t ShrinkingLife = 9;
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4, 0.4f
    constexpr double Swell = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1.2f
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3f6b851fu));   // 0x6ba3cc, 0.92f
    particle.position.z = F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.position.z));
    const double scale = static_cast<double>(particle.scale);
    particle.scale = F32(particle.life >= ShrinkingLife ? scale - Brake : scale * Swell);
    if (particle.subtype == StormMistSubtype)
        ScaleLight(particle, Dim);
    else
        particle.light = glm::vec3(1.0f);
}

// 0x5468de..0x546900
void StepTheShortStormMist(Particle& particle)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    constexpr float Green = 3.0f;                                                     // 0x5468ea, 0x40400000
    particle.scale = F32(static_cast<double>(particle.scale) + Growth);
    particle.light = glm::vec3(1.0f, Green, 1.0f);
    particle.position.z = F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.position.z));
}
} // namespace

bool IsStormMist(std::int32_t type, std::int32_t subtype)
{
    return type == BreathParticle &&
           (subtype == StormMistSubtype || subtype == DarkStormMistSubtype || subtype == ShortStormMistSubtype);
}

Core::Expected<void, ParticleError> CreateStormMist(Particle& particle, Crt::CrtRandom& random,
                                                    const ParticleTerrainHeight& terrainHeight, const float* worldClock,
                                                    IParticleTrigonometry* trigonometry)
{
    constexpr std::int32_t MistLife = 12, ShortLife = 4;
    constexpr float MistScale = std::bit_cast<float>(0x40133333u);  // 0x5425ce, 2.3f
    constexpr float ShortScale = std::bit_cast<float>(0x3fe66666u); // 0x54266f, 1.8f
    constexpr float MistRise = 33.0f;                               // 0x5425e0, 0x42040000
    constexpr float ShortRise = 15.0f;                              // 0x542676, 0x41700000
    constexpr double AboveGround = 15.0;                            // 0x6b87bc
    constexpr float Sway = 5.0f;                                    // 0x6b87f8
    const bool shortMist = particle.subtype == ShortStormMistSubtype;
    // 0x5425c7..0x5425f6, 0x54261b..0x54264a and 0x542668..0x542697.
    particle.life = shortMist ? ShortLife : MistLife;
    particle.scale = shortMist ? ShortScale : MistScale;
    particle.verticalSpeed = shortMist ? ShortRise : MistRise;
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    // 0x5425f9, 0x54264d and 0x54269a: 0x5f93c0 with the particle's x and y (edi is +0x14 since 0x53feb8), retained
    // until 15 is added.
    particle.position.z = F32(terrainHeight(particle.position.x, particle.position.y) + AboveGround);
    // 0x54260a: /28 is drawn with Effect\PoundingBall.jpg (0x6ad).
    if (particle.subtype == StormMistSubtype)
        particle.texture = CinderParticle;
    if (!shortMist)
        return {};
    // 0x5426ae..0x5426c7: s = fsin(clock) · 5 in PC53, retained; the x and the y add s.
    if (worldClock == nullptr || trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    const auto sway = trigonometry->SineProductPC53(static_cast<double>(*worldClock), Sway);
    if (!sway.HasValue())
        return Core::Unexpected(sway.Error());
    particle.position.x = F32(sway.Value() + static_cast<double>(particle.position.x));
    particle.position.y = F32(sway.Value() + static_cast<double>(particle.position.y));
    return {};
}

bool IsSmokeParticle(std::int32_t type, std::int32_t subtype)
{
    if (type == RisingPuffParticle)
        return true;
    if (type != BreathParticle)
        return false;
    switch (subtype)
    {
    case EmberSmokeSubtype:
    case GlowSmokeSubtype:
    case ShakingSmokeSubtype:
    case DarkShakingSmokeSubtype:
    case WarmBreathSubtype:
    case StoneSmokeSubtype:
    case FadingSmokeSubtype:
    case ShrinkingSmokeSubtype:
    case TrailSmokeSubtype:
    case BackTrailSmokeSubtype:
    case StormMistSubtype:
    case DarkStormMistSubtype:
    case ShortStormMistSubtype:
        return true;
    default:
        return false;
    }
}

void CreateSmokeParticle(Particle& particle, Crt::CrtRandom& random, float clock)
{
    constexpr std::int32_t GlowLife = 12, ShakingLife = 40, BreathLife = 16, StoneLife = 32, FadingLife = 25;
    constexpr std::int32_t ShrinkingLife = 15, EmberLife = 20;
    constexpr int BreathMask = 31, BreathBase = 48, FadingMask = 63, FadingBase = 64;
    constexpr float Ember = std::bit_cast<float>(0x3e99999au); // 0x541973, 0.3f
    constexpr double BackStep = 15.0;                          // 0x6b87bc
    if (particle.type == RisingPuffParticle)
    {
        CreateThePuff(particle, random, clock);
        return;
    }
    switch (particle.subtype)
    {
    case EmberSmokeSubtype:
        // 0x541c18..0x541c7d
        particle.life = EmberLife;
        particle.scale = F32(static_cast<double>((random.Next() & FadingMask) + FadingBase) * Hundredth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case GlowSmokeSubtype:
        // 0x5418b1..0x541924
        KeepTheLight(particle);
        particle.life = GlowLife;
        ShakeAtBirth(particle, random, clock);
        return;
    case ShakingSmokeSubtype:
        // 0x54228c..0x5422ff: its step does not read the kept light.
        KeepTheLight(particle);
        particle.life = ShakingLife;
        ShakeAtBirth(particle, random, clock);
        return;
    case DarkShakingSmokeSubtype:
        // 0x542307..0x542368: vida 40.
        particle.life = ShakingLife;
        ShakeAtBirth(particle, random, clock);
        return;
    case WarmBreathSubtype:
        // 0x54184b..0x5418a9, the creation of the breath /0.
        particle.life = BreathLife;
        particle.scale = F32(static_cast<double>((random.Next() & BreathMask) + BreathBase) * Hundredth);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(Math::FtolLow(static_cast<double>(clock)) % FullTurn);
        return;
    case StoneSmokeSubtype:
    {
        // 0x542450..0x5424b7
        particle.life = StoneLife;
        const double argumentScale = static_cast<double>(particle.scale);
        particle.scale =
            F32((static_cast<double>((random.Next() & BreathMask) + BreathBase) * Hundredth + 1.0) * argumentScale);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(Math::FtolLow(static_cast<double>(clock)) % FullTurn);
        return;
    }
    case FadingSmokeSubtype:
        // 0x54252c..0x542591
        particle.life = FadingLife;
        particle.scale = F32(static_cast<double>((random.Next() & FadingMask) + FadingBase) * Hundredth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case ShrinkingSmokeSubtype:
        // 0x5426cf..0x542717: the requested scale (the one it already has).
        particle.life = ShrinkingLife;
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case TrailSmokeSubtype:
        // 0x54196c..0x54197a
        particle.light = glm::vec3(1.0f, Ember, 0.0f);
        TrailAtBirth(particle, random);
        return;
    case BackTrailSmokeSubtype:
    {
        // 0x541932..0x541967: the requested light is a direction, kept in +0x60..+0x68; the z and the y move back along
        // it (not the x).
        const glm::vec3 direction = particle.light;
        KeepTheLight(particle);
        particle.light = glm::vec3(1.0f);
        particle.position.z =
            F32(static_cast<double>(particle.position.z) - static_cast<double>(direction.z) * BackStep);
        particle.position.y =
            F32(static_cast<double>(particle.position.y) - static_cast<double>(direction.y) * BackStep);
        TrailAtBirth(particle, random);
        return;
    }
    default:
        return;
    }
}

Core::Expected<void, ParticleError> StepSmokeParticle(Particle& particle, Crt::CrtRandom& random,
                                                      const ParticleOwnerStates& ownerStates)
{
    constexpr double Fortieth = static_cast<double>(std::bit_cast<float>(0x3ccccccdu));     // 0x6ba1fc, 0.025f
    constexpr double ThirtySecond = 0.03125;                                                // 0x6b8880
    constexpr double Ember = static_cast<double>(std::bit_cast<float>(0x3f19999au));        // 0x6b8a20, 0.6f
    constexpr double Ash = static_cast<double>(std::bit_cast<float>(0x3eb33333u));          // 0x6b8bc4, 0.35f
    constexpr double FadingGrowth = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0.15f
    constexpr double BreathRise = Fifth;                                                    // 0x6b8854
    if (particle.type == RisingPuffParticle)
    {
        StepThePuff(particle);
        return {};
    }
    switch (particle.subtype)
    {
    case EmberSmokeSubtype:
    {
        // 0x5462c8..0x5462f1: l = life · 0.05f, retained; the scale grows 0.09f (0x6ba3ac).
        constexpr double EmberGrowth = static_cast<double>(std::bit_cast<float>(0x3db851ecu));
        const double ember = static_cast<double>(particle.life) * Twentieth;
        particle.light = glm::vec3(F32(ember), F32(ember * Half), F32(ember * Tenth));
        particle.scale = F32(static_cast<double>(particle.scale) + EmberGrowth);
        return {};
    }
    case GlowSmokeSubtype:
    {
        // 0x546171..0x5461a0
        double glow = static_cast<double>(particle.life) * Eighth;
        if (glow > 1.0)
            glow = 1.0 - glow;
        particle.light = glm::vec3(F32(glow * static_cast<double>(particle.rotationFactor)),
                                   F32(glow * static_cast<double>(particle.field64.x)),
                                   F32(glow * static_cast<double>(particle.field64.y)));
        RiseLikeTheBreath(particle, BreathRise);
        return {};
    }
    case ShakingSmokeSubtype:
    case DarkShakingSmokeSubtype:
    {
        // 0x546573 (/19) and 0x54661e (/20)
        double gray = static_cast<double>(particle.life) * Fortieth;
        if (particle.subtype == ShakingSmokeSubtype)
            gray *= Half;
        particle.light = glm::vec3(F32(gray));
        Shake(particle, random);
        return {};
    }
    case WarmBreathSubtype:
    {
        // 0x5460a0..0x5460bd
        const double warm = static_cast<double>(particle.life) * Eighth;
        particle.light = glm::vec3(F32(warm * Tenth), F32(warm), F32(warm * Ember));
        RiseLikeTheBreath(particle, BreathRise);
        return {};
    }
    case StoneSmokeSubtype:
    {
        // 0x54675f..0x546790
        const double ash = static_cast<double>(particle.life) * ThirtySecond;
        particle.light = glm::vec3(F32(ash * Fifth), F32(ash * Half), F32(ash * Ash));
        RiseLikeTheBreath(particle, Tenth);
        return {};
    }
    case FadingSmokeSubtype:
        // 0x5467ed..0x546815
        ScaleLight(particle, static_cast<double>(particle.life) * Twentieth);
        particle.scale = F32(static_cast<double>(particle.scale) + FadingGrowth);
        return {};
    case ShrinkingSmokeSubtype:
        StepTheShrinking(particle);
        return {};
    case StormMistSubtype:
    case DarkStormMistSubtype:
        StepTheStormMist(particle);
        return {};
    case ShortStormMistSubtype:
        StepTheShortStormMist(particle);
        return {};
    case TrailSmokeSubtype:
        return StepTheTrail(particle, ownerStates);
    case BackTrailSmokeSubtype:
        StepTheBackTrail(particle);
        return {};
    default:
        return {};
    }
}

} // namespace Client::Particles
