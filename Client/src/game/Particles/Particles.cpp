#include "Particles/Particles.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Math/Ftol.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/NoriaParticleKernels.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/TrialsParticleKernels.h"
#include "Particles/WeaponSparkKernels.h"
#include "Particles/BloodCastleParticleKernels.h"
#include "Particles/EffectParticleKernels.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/MissileFireParticleKernels.h"
#include "Particles/ParticleOwner.h"
#include "WorldObjects/WorldObjectStepServices.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace Client::Particles
{
namespace
{
// 0x53fe70: without a free slot the binding is not queried and no rand() is taken. Compatible entries search in
// CreateImpl; it only anticipates if the type would be rejected.
bool IsParticlePoolFull(std::span<const Particle> particles)
{
    return std::ranges::all_of(particles, [](const Particle& particle) { return particle.alive; });
}

constexpr std::int32_t BreathSubtype = 0;
// Extensions of 0x53fe70/0x5446e0/0x548550 for 67/0 and 68/0 (the types, in Particles.h); resources and backend are
// validated separately. The 68 is born like the 67 and then shifts its position.
constexpr std::int32_t Particle67Life = 20;
// Extension of 0x5446e0 for the cloud 622/8: +0x38 uses these binary operands.
constexpr double Cloud8Rise = std::bit_cast<double>(std::uint64_t{0x3fa47ae147ae147bull});
constexpr float Cloud8RiseLimit = std::bit_cast<float>(0x3f800000u);
constexpr float Cloud8FallLimit = std::bit_cast<float>(0x3dcccccdu);
constexpr float Cloud8Fall = std::bit_cast<float>(0x3ccccccdu);
constexpr double Cloud8RandomStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr float Cloud8InitialQ = std::bit_cast<float>(0x3dcccccdu);
// 5446e0_ext_67 and the full historical contract leaf-4303c0 (predicate raw32 == 34, no calls).
constexpr std::int32_t Particle67ColorMap = 34;
constexpr double Particle67Growth = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));
constexpr double Particle67Fade = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr float Particle67Blue = std::bit_cast<float>(0x3f333333u);
constexpr int Particle67ScaleSpan = 6;
constexpr int Particle67ScaleBase = 8;
constexpr double Particle67ScaleStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
// The 68 shifts each axis rand() % 10 − 5, in x, y, z.
constexpr int Particle68OffsetSpan = 10;
constexpr int Particle68OffsetCenter = 5;
// 0x53fe70/0x5446e0, extension 5c6/9 (map 3, object 322).
constexpr std::int32_t MapSparkLife = 30;
constexpr int MapSparkOffsetSpan = 10;
constexpr double MapSparkOffsetBase = 160.0;
constexpr float MapSparkForward = 12.0f;
constexpr float MapSparkDown = -2.0f;
constexpr double MapSparkBrake = static_cast<double>(std::bit_cast<float>(0x3f99999au));
constexpr double MapSparkFall = 1.0;
constexpr double MapSparkShrink = static_cast<double>(std::bit_cast<float>(0x3da3d70au));
constexpr double MapSparkMinimum = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
// Extensiones final-runtime 53fe70/5446e0/548550, 607/0 (mapa7, objeto22).
constexpr int DropLifeSpan = 10;
constexpr int DropLifeBase = 30;
constexpr int DropScaleSpan = 6;
constexpr int DropScaleBase = 4;
constexpr double DropScaleStep = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));
constexpr int DropWalkSpan = 20;
constexpr int DropWalkCenter = 10;
constexpr double DropWalkFactor = 2.5;
// 0x542d60..0x542d6e: the 607 from 0 to 4 (the table of 0x544614); those outside leave through 0x54304c with the
// prefix.
constexpr std::int32_t LastDropSubtype = 4;
// The /1 of the bubbles (0x43c332, 0x52434e, 0x526d5f), the /2 that asks for sound 0x1d8 (0x4ef02e), the /3 that rises
// with its +0x50 (0x523089) and the /4 that stops at 350 (0x52a400).
constexpr std::int32_t BubbleDropSubtype = 1, SoundingDropSubtype = 2, RisingDropSubtype = 3, CappedDropSubtype = 4;
// final-runtime contracts 0x53fe70/0x5446e0/0x548550, cases 5d9/0..3 and 5f2/2.
constexpr std::int32_t FireLife = 24;
constexpr std::int32_t WorldSmokeLife = 50;
constexpr double FireRise = static_cast<double>(std::bit_cast<float>(0x3b83126fu));
constexpr double FireShrink = static_cast<double>(std::bit_cast<float>(0x3d23d70au));
constexpr double FireDamping = static_cast<double>(std::bit_cast<float>(0x3f7ae148u));
constexpr double SmokeFade = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
constexpr double SmokeSlowdown = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double SmokeDrift = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr double FireSpeed = static_cast<double>(std::bit_cast<float>(0xbdcccccdu));
constexpr int FireSpeedSpan = 16;
constexpr int FireSpeedBase = 32;
constexpr int FireScaleSpan = 64;
constexpr int FireScaleBase = 128;
constexpr int FireSmallScaleSpan = 4;
constexpr int FireSmallScaleBase = 10;
constexpr int SmokeScaleBase = 64;
constexpr int SmokeSpeedBase = 60;
constexpr int FrameSteps = 6;
constexpr double FireHeightFactor = 10.0;
// 0x53ff05: the life of any particle when created; 0x54184b: the breath's.
constexpr std::int32_t DefaultLife = 2;
constexpr std::int32_t BreathLife = 16;
// 0x5406eb: the crow spark's, and what it adds to its rand() % 2 (0x6b8818).
constexpr std::int32_t SparkLife = 10;
constexpr float SparkPush = 2.0f;
constexpr double SparkShrink = static_cast<double>(std::bit_cast<float>(0x3F666666u));
constexpr double SparkFade = static_cast<double>(std::bit_cast<float>(0x3F788B30u));
// 0x543407..0x5434d3: the login mist, five rand() and binary32 constants of the PE.
constexpr std::int32_t CloudLife = 500;
constexpr int CloudOffsetSpan = 400;
constexpr int CloudOffsetCenter = 200;
constexpr int CloudScaleSpan = 15;
constexpr int CloudScaleBase = 10;
constexpr int CloudSpeedSpan = 10;
constexpr int CloudSpeedBase = 5;
constexpr double CloudScaleStep = static_cast<double>(std::bit_cast<float>(0x3d888889u));
constexpr double CloudSpeedStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));
// 0x5475b6..0x547699 and 0x548c04..0x548c2f; 06 § Los efectos del login, the mist.
constexpr float CloudRotationFactor = 1.5f;
constexpr double CloudGrowth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr float CloudLightLimit = std::bit_cast<float>(0x3e19999au);
constexpr double CloudFade = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));
constexpr double CloudGrowthRadius = 300.0;
constexpr double CloudDeathRadius = 500.0;
constexpr double BreathScaleStep = static_cast<double>(0.01f); // 0x6b88d4
constexpr int BreathScaleBase = 48;
constexpr int FullTurn = Math::FullTurn;
// 0x545ec4: the light is the life by 0.125 (0x6ba170), the vertical speed adds 0.2 (0x6b8854) and the scale, 0.05
// (0x6b8a48).
constexpr double BreathLightPerLife = 0.125;
constexpr double BreathRise = static_cast<double>(0.2f);
constexpr double BreathGrowth = static_cast<double>(0.05f);
// 0x54271f..0x542838: the 5f3/0, life 32, (rand() & 15) − 8 in x and y, ((rand() & 31) + 32) · scale · 0.01f and the
// push (0; 3; 0) turned by the requested angles.
constexpr std::int32_t DustLife = 32;
constexpr int DustOffsetSpan = 16;
constexpr int DustOffsetCenter = 8;
constexpr int DustScaleSpan = 32;
constexpr int DustScaleBase = 32;
constexpr glm::vec3 DustPush{0.0f, 3.0f, 0.0f};
// 0x546b66..0x546bfb: the light by 0.03125 (0x6b8880), the scale plus 0.08f (0x6ba1ac), the brake 0.9f (0x6b8a1c) and
// the texture's half height (0x6b882c).
constexpr double DustLightPerLife = static_cast<double>(std::bit_cast<float>(0x3d000000u));
constexpr double DustGrowth = static_cast<double>(std::bit_cast<float>(0x3da3d70au));
constexpr double DustBrake = static_cast<double>(std::bit_cast<float>(0x3f666666u));
constexpr double DustHalfHeight = static_cast<double>(std::bit_cast<float>(0x3f000000u));
// 0x542a69 and 0x545cac..0x545ccd: the 5ed/0's life, the 0.05f of its light (0x6b8ad0), the three color factors
// (0x6b882c, 0x6b89dc and 0x6b87b8) and the radius.
constexpr std::int32_t ExplosionLife = 20;
constexpr double ExplosionGlow = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double ExplosionRed = static_cast<double>(std::bit_cast<float>(0x3f000000u));
constexpr double ExplosionGreen = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double ExplosionBlue = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr std::int32_t ExplosionLightRadius = 4;
// 0x542b11 and 0x542b1b: the 5fd's life and its x angle (0x42340000). 0x546d08..0x546d2e: the sine step, 5° in float
// (0x6ba368), its factor (0x6b87f8), that of the /2 onward (0x6ba36c) and how much that one turns (0x6b8864).
constexpr std::int32_t HandShineLife = 0x24;
constexpr float HandShineTilt = std::bit_cast<float>(0x42340000u);
constexpr float HandShinePulseStep = std::bit_cast<float>(0x3db2b8c2u);
constexpr float HandShinePulse = std::bit_cast<float>(0x40a00000u);
constexpr double HandShineSecondScale = static_cast<double>(std::bit_cast<float>(0x3f400000u));
constexpr double HandShineTurn = static_cast<double>(std::bit_cast<float>(0x41400000u));
// 0x5446e0: the wind change, (rand() % 2001 − 1000) · 0.0006 (0x6b8a50), between ±0.6 (0x6b8a20 and 0x6b8b8c), and
// the wind, between ±1.7 (0x6ba3f0 and 0x6ba3ec).
constexpr int WindSpan = 2001;
constexpr int WindCenter = 1000;
constexpr double WindStep = static_cast<double>(0.0006f);
constexpr float ChangeLimit = 0.6f;
constexpr float WindLimit = 1.7f;

using Math::F32;

// The clamp of 0x544730..0x544773 (and of 0x54479a..0x5447de), with the x87 comparisons: if it is not greater than or
// equal to the upper one (C0, or unordered) and the lower one is greater, the lower one; if it is then still not
// greater than or equal to the upper one, the value; otherwise, the upper one. A NaN passes.
float ClampLikeTheBinary(float value, float low, float high)
{
    const bool belowHigh = value < high || std::isnan(value);
    if (belowHigh && !(low < value || low == value || std::isnan(value)))
    {
        return low;
    }
    return belowHigh ? value : high;
}

// 0x4e98d0: the position plus the speed rotated by AngleMatrix of the angles, each sum in 53 bits.
void Move(Particle& particle)
{
    const glm::vec3 turned = Math::OriginalVectorRotate(particle.velocity, Math::OriginalAngleMatrix(particle.angles));
    for (int k = 0; k < 3; ++k)
    {
        particle.position[k] = F32(static_cast<double>(turned[k]) + static_cast<double>(particle.position[k]));
    }
}

void StepTheWind(ParticleWind& wind, Crt::CrtRandom& random)
{
    for (int k = 0; k < 2; ++k)
    {
        const int steps = (random.Next() % WindSpan) - WindCenter;
        const float changed = F32((static_cast<double>(steps) * WindStep) + static_cast<double>(wind.change[k]));
        wind.change[k] = ClampLikeTheBinary(changed, -ChangeLimit, ChangeLimit);
    }
    for (int k = 0; k < 2; ++k)
    {
        const float blown = F32(static_cast<double>(wind.change[k]) + static_cast<double>(wind.wind[k]));
        wind.wind[k] = ClampLikeTheBinary(blown, -WindLimit, WindLimit);
    }
}

// 0x545ec4: the breath rises, grows and fades with the life. The z adds the vertical speed before it is stored in a
// float (fst leaves it on the x87 stack).
void StepTheBreath(Particle& particle)
{
    const float light = F32(static_cast<double>(particle.life) * BreathLightPerLife);
    particle.light = glm::vec3(light);
    const double rise = static_cast<double>(particle.verticalSpeed) + BreathRise;
    particle.verticalSpeed = F32(rise);
    particle.position.z = F32(rise + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + BreathGrowth);
}

bool IsBreath(const Particle& particle)
{
    return particle.type == BreathParticle && particle.subtype == BreathSubtype;
}

// 0x54271f and 0x546b66: the 5f3 and the 5f6 share creation and step; MuRet accepts both with the /0 and the /1.
bool IsDustType(std::int32_t type)
{
    return type == DustParticle || type == ArrivalDustParticle;
}

bool IsDust(const Particle& particle)
{
    return IsDustType(particle.type) && (particle.subtype == 0 || particle.subtype == 1);
}

// 0x542a60..0x542a75 with subtype 0: life 20 (the /2 has 30) and the requested scale again in +0x10. No rand(). The
// sound of 0x542a82 is played by whoever creates it.
void CreateTheExplosion(Particle& particle, float scale)
{
    particle.life = particle.subtype == 2 ? 30 : ExplosionLife;
    particle.scale = scale;
}

// 0x5459a4..0x5459bc and 0x545c91..0x545cd6 with subtype 0, after the life and the common movement: the frame (+0x48)
// is (20 − life) / 2 with the idiv quotient (cdq, sub and sar: toward 0), and the dynamic light of radius 4 is
// (f · 0.5f; f · 0.3f; f · 0.1f) with f = life · 0.05f (0x6b8ad0) retained on the x87 across the three products.
Core::Expected<void, ParticleError> StepTheExplosion(Particle& particle, Particles& particles, Crt::CrtRandom& random,
                                                     const float* worldClock, Terrain::TerrainLighting* lighting,
                                                     const std::function<void(Audio::SoundId)>& sound)
{
    particle.field48 = std::bit_cast<float>((ExplosionLife - particle.life) / 2);
    if (particle.subtype == 2)
    {
        const int frameLife = particle.life < 11 ? 1 : particle.life - 10;
        particle.field48 = std::bit_cast<float>((ExplosionLife - frameLife) / 2);
        if (particle.life == 20 || particle.life == 10 || particle.life == 1)
        {
            const float radius = particle.life == 20 ? 200.0f : particle.life == 10 ? 400.0f : 600.0f;
            const float glow = particle.life == 20 ? 1.0f : particle.life == 10 ? 0.6f : 0.3f;
            for (int index = 0; index < 18; ++index)
            {
                const glm::vec3 angles{0, 0, static_cast<float>(index * 20)};
                const auto offset = Math::OriginalVectorRotate({0, radius, 0}, Math::OriginalAngleMatrix(angles));
                glm::vec3 point;
                for (int axis = 0; axis < 3; ++axis)
                    point[axis] = F32(static_cast<double>(offset[axis]) + static_cast<double>(particle.position[axis]));
                const auto smoke = particles.CreateChecked(BreathParticle, point, particle.angles, glm::vec3(glow), 35,
                                                           2.5f, worldClock == nullptr ? 0.0f : *worldClock, random);
                if (!smoke.HasValue())
                    return Core::Unexpected(smoke.Error());
                const auto burst = particles.CreateChecked(ExplosionParticle, point, particle.angles, glm::vec3(glow),
                                                           1, 1, worldClock == nullptr ? 0.0f : *worldClock, random);
                if (!burst.HasValue())
                    return Core::Unexpected(burst.Error());
                if (burst.Value() && sound)
                    sound(Sound::ExplosionSound);
            }
        }
    }
    if (particle.subtype == 1)
        return {};
    const double glow = static_cast<double>(particle.life) * ExplosionGlow;
    const glm::vec3 color{F32(glow * ExplosionRed), F32(glow * ExplosionGreen), F32(glow * ExplosionBlue)};
    if (!WorldObjects::AddDynamicLight(lighting, particle.position, color, ExplosionLightRadius, "0x545cd6").HasValue())
        return Core::Unexpected(ParticleError::MissingDynamicLight);
    return {};
}

// 0x540260..0x540316: the life (rand() & 7) + 20 for all; the /2 overrides it with 19 and shifts the y rand() % 60 −
// 30 (0x5402ac); the /3 lives 7 and the /4 4, both with texture 0x5fd (0x5402df, 0x5402f2), and the /4 turns
// rand() % 180 − 360 (0x540310).
void CreateTheMagicSpark(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t Shine = 0x5fd;
    constexpr std::int32_t ShiftedLife = 19, FlashLife = 7, BlinkLife = 4;
    constexpr int ShiftSpan = 60, TurnSpan = 180;
    constexpr double ShiftCenter = 30.0, TurnBase = 360.0;
    particle.life = (random.Next() & 7) + 20;
    if (particle.subtype == 2)
    {
        particle.life = ShiftedLife;
        particle.position.y = F32(
            (static_cast<double>(random.Next() % ShiftSpan) + static_cast<double>(particle.position.y)) - ShiftCenter);
    }
    else if (particle.subtype == 3)
    {
        particle.life = FlashLife;
        particle.texture = Shine;
    }
    else if (particle.subtype == 4)
    {
        particle.life = BlinkLife;
        particle.texture = Shine;
        particle.rotation = F32(static_cast<double>(random.Next() % TurnSpan) - TurnBase);
    }
}

// 0x544e87..0x544ed5: the /2 shrinks by 0.92f; the /3 grows by 1.3f and fades by 0.9f; the /4 jumps to the end
// (0x544edd) with nothing of its own.
void StepTheMagicSpark(Particle& particle)
{
    if (particle.subtype == 2)
    {
        particle.scale =
            F32(static_cast<double>(particle.scale) * static_cast<double>(std::bit_cast<float>(0x3f6b851fu)));
    }
    else if (particle.subtype == 3)
    {
        particle.scale =
            F32(static_cast<double>(particle.scale) * static_cast<double>(std::bit_cast<float>(0x3fa66666u)));
        for (int channel = 0; channel < 3; ++channel)
            particle.light[channel] = F32(static_cast<double>(particle.light[channel]) *
                                          static_cast<double>(std::bit_cast<float>(0x3f666666u)));
    }
}

// 0x543939..0x5439c9 (the /0) and 0x5439ca..0x543a5c (the /1): life 24 or 15, the speed (0; −(rand() % 16 + 32) ·
// 0.1f; 0) from the fild by −0.1f (0x6b8930), the scale (rand() % 128 + 128) · 0.01f (0x6b88d4), the light 0.5 and
// the turn rand() % 360. Three rand(); with another subtype, 0x54304c with nothing of its own.
void CreateTheCinder(Particle& particle, Crt::CrtRandom& random)
{
    if (particle.subtype != 0 && particle.subtype != 1)
        return;
    constexpr double Fall = static_cast<double>(std::bit_cast<float>(0xbdcccccdu));
    constexpr double Hundredth = Math::HundredthAsDouble;
    using Math::Half;
    constexpr std::int32_t MeteorLife = 24, JavelinLife = 15;
    particle.life = particle.subtype == 0 ? MeteorLife : JavelinLife;
    particle.velocity.x = 0.0f;
    const int fall = static_cast<int>(random.Next() & 15) + 32;
    particle.velocity.z = 0.0f;
    particle.velocity.y = F32(static_cast<double>(fall) * Fall);
    const int size = static_cast<int>(random.Next() & 127) + 128;
    particle.scale = F32(static_cast<double>(size) * Hundredth);
    const int turn = static_cast<int>(random.Next() % 360);
    particle.light = glm::vec3(Half);
    particle.rotation = static_cast<float>(turn);
}

// 0x5478d7..0x54793a, after the life and the common movement, with any subtype: the vertical speed rises 0.004f
// (0x6b89e8), the scale drops 0.02f (0x6b8b90), the frame (+0x48) is (23 − life) / 6 (imul by 0x2aaaaaab, toward 0)
// and the z rises the vertical speed (reread from the float) times 10. The /1 fades with life · 0.1f (0x6b8a34), one
// fst per channel.
void StepTheCinder(Particle& particle)
{
    constexpr double Rise = static_cast<double>(std::bit_cast<float>(0x3b83126fu));
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    constexpr double Tenth = Math::TenthAsDouble;
    constexpr std::int32_t FrameEnd = 23, FrameLength = 6;
    particle.verticalSpeed = F32(static_cast<double>(particle.verticalSpeed) + Rise);
    const std::int32_t life = particle.life;
    particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
    particle.field48 = std::bit_cast<float>((FrameEnd - life) / FrameLength);
    particle.position.z =
        F32(static_cast<double>(particle.verticalSpeed) * 10.0 + static_cast<double>(particle.position.z));
    if (particle.subtype == 1)
        particle.light = glm::vec3(F32(static_cast<double>(life) * Tenth));
}

// 0x54271f..0x542838: the life, the position shifted in x and y, the scale on the x87 (fild, by the requested scale
// and by 0.01f; 0x542761 splits the 5f6 from the 5f3 in two equal branches), the speed turned by AngleMatrix (0x5fc8f0
// and 0x5fcbd0) and the x turn at rand() % 360. With subtype 0 (0x5427aa), the push (0; 3; 0) and the requested
// angles: four rand(). With another, first the z turn at rand() % 360, the push (0; 15; 0) and the slot's angles; the
// /1 (0x54282c, the 5f6 of the path and the 5f3 of the +0x3e5 brake) also lives 40, stays still and shifts the z: six.
void CreateTheDust(Particle& particle, const glm::vec3& angles, float scale, Crt::CrtRandom& random)
{
    particle.life = DustLife;
    const int x = (random.Next() % DustOffsetSpan) - DustOffsetCenter;
    particle.position.x = F32(static_cast<double>(x) + static_cast<double>(particle.position.x));
    const int y = (random.Next() % DustOffsetSpan) - DustOffsetCenter;
    particle.position.y = F32(static_cast<double>(y) + static_cast<double>(particle.position.y));
    const int size = (random.Next() % DustScaleSpan) + DustScaleBase;
    particle.scale = F32((static_cast<double>(size) * static_cast<double>(scale)) * BreathScaleStep);
    const bool turned = particle.subtype != 0;
    if (turned)
        particle.angles.z = static_cast<float>(random.Next() % FullTurn);
    constexpr glm::vec3 ArrivalPush{0, 15, 0};
    particle.velocity = Math::OriginalVectorRotate(turned ? ArrivalPush : DustPush,
                                                   Math::OriginalAngleMatrix(turned ? particle.angles : angles));
    particle.angles.x = static_cast<float>(random.Next() % FullTurn);
    if (particle.subtype == 1)
    {
        constexpr std::int32_t ArrivalLife = 40;
        particle.life = ArrivalLife;
        particle.velocity = {};
        const int z = (random.Next() % DustOffsetSpan) - DustOffsetCenter;
        particle.position.z = F32(static_cast<double>(z) + static_cast<double>(particle.position.z));
    }
}

// 0x546b66..0x546bfb: after the common movement (0x4e98d0, with the turned speed), the light by the life, the growing
// scale, the position plus the speed again, unturned (Q-C402), and the brake. The z is the terrain height (0x5f93c0)
// plus the height of the type's texture (+0x24 of its slot) by the already grown scale and by 0.5. The comparison at
// 0x546b69 lets the /5 float, which MuRet does not create. Without the height or the texture height, it cuts with the
// rest written.
Core::Expected<void, ParticleError> StepTheDust(Particle& particle, const ParticleTerrainHeight& terrainHeight,
                                                const ParticleTextureHeight& textureHeight)
{
    particle.light = glm::vec3(F32(static_cast<double>(particle.life) * DustLightPerLife));
    particle.scale = F32(static_cast<double>(particle.scale) + DustGrowth);
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.position[axis] =
            F32(static_cast<double>(particle.velocity[axis]) + static_cast<double>(particle.position[axis]));
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * DustBrake);
    }
    if (!terrainHeight)
    {
        return Core::Unexpected(ParticleError::MissingTerrainHeight);
    }
    const double ground = terrainHeight(particle.position.x, particle.position.y);
    const std::optional<float> height = textureHeight ? textureHeight(particle.type) : std::nullopt;
    if (!height)
    {
        return Core::Unexpected(ParticleError::MissingTextureSize);
    }
    particle.position.z =
        F32(((static_cast<double>(*height) * static_cast<double>(particle.scale)) * DustHalfHeight) + ground);
    return {};
}

bool IsFlowerType(std::int32_t type)
{
    return type >= FirstFlowerParticle && type <= LastFlowerParticle;
}

// 0x542c4b..0x542d00: the flowers 608..60a after the prefix, without reading the subtype: the life rand() % 10 + 15,
// the light at 1, the scale (rand() % 8 + 4) · 0.03f (0x6b8bd4), which overrides the requested one, and the speed
// ((rand() % 32 − 16) · 0.1f; (rand() % 32 − 16) · 0.1f; (rand() % 32 − 32) · 0.1f) (0x6b87b8). Five rand(); the and
// with the sign correction is the plain %.
void CreateTheFlower(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int LifeSpan = 10, LifeBase = 15, SizeSpan = 8, SizeBase = 4;
    constexpr int DriftSpan = 32, DriftCenter = 16, FallCenter = 32;
    constexpr double SizeStep = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));  // 0x6b8bd4: 0.03f
    constexpr double DriftStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8: 0.1f
    particle.life = random.Next() % LifeSpan + LifeBase;
    const int size = random.Next() % SizeSpan + SizeBase;
    particle.light = glm::vec3(1.0f);
    particle.scale = F32(static_cast<double>(size) * SizeStep);
    particle.velocity.x = F32(static_cast<double>(random.Next() % DriftSpan - DriftCenter) * DriftStep);
    particle.velocity.y = F32(static_cast<double>(random.Next() % DriftSpan - DriftCenter) * DriftStep);
    particle.velocity.z = F32(static_cast<double>(random.Next() % DriftSpan - FallCenter) * DriftStep);
}

// 0x546da6..0x546f08: the flowers, after the common movement (0x4e98d0, with the turned speed). While +0x48 (an
// integer) is 0, in the air: the /0 adds (rand() % 32 − 16) · 0.1f to each speed axis; the /1 adds (rand() % 8 − 4) ·
// 0.05f (0x6b8a48) to the x and the y and subtracts (rand() % 8) · 0.05f from the z; the others, nothing; and the
// position plus the speed again, unturned. Always: with the terrain height (0x5f93c0) above the z (`fcom` and
// `test ah, 0x41`: neither less, nor equal, nor NaN), the z becomes the height, +0x48 the integer 1 (landed) and the
// speed 0; and the turn plus rand() % 16. Without the height it cuts with the rest written.
Core::Expected<void, ParticleError> StepTheFlower(Particle& particle, Crt::CrtRandom& random,
                                                  const ParticleTerrainHeight& terrainHeight)
{
    constexpr int DriftSpan = 32, DriftCenter = 16, SwaySpan = 8, SwayCenter = 4, TurnSpan = 16;
    constexpr double DriftStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8: 0.1f
    constexpr double SwayStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));  // 0x6b8a48: 0.05f
    constexpr std::uint32_t Landed = 1;
    constexpr std::int32_t DriftingSubtype = 0, SwayingSubtype = 1;
    if (std::bit_cast<std::uint32_t>(particle.field48) == 0)
    {
        if (particle.subtype == DriftingSubtype)
        {
            for (int axis = 0; axis < 3; ++axis)
                particle.velocity[axis] = F32(static_cast<double>(random.Next() % DriftSpan - DriftCenter) * DriftStep +
                                              static_cast<double>(particle.velocity[axis]));
        }
        else if (particle.subtype == SwayingSubtype)
        {
            for (int axis = 0; axis < 2; ++axis)
                particle.velocity[axis] = F32(static_cast<double>(random.Next() % SwaySpan - SwayCenter) * SwayStep +
                                              static_cast<double>(particle.velocity[axis]));
            const double sink = static_cast<double>(random.Next() % SwaySpan) * SwayStep;
            particle.velocity.z = F32(static_cast<double>(particle.velocity.z) - sink);
        }
        for (int axis = 0; axis < 3; ++axis)
            particle.position[axis] =
                F32(static_cast<double>(particle.velocity[axis]) + static_cast<double>(particle.position[axis]));
    }
    if (!terrainHeight)
        return Core::Unexpected(ParticleError::MissingTerrainHeight);
    const double ground = terrainHeight(particle.position.x, particle.position.y);
    if (ground > static_cast<double>(particle.position.z))
    {
        particle.position.z = F32(ground);
        particle.field48 = std::bit_cast<float>(Landed);
        particle.velocity = glm::vec3(0.0f);
    }
    particle.rotation = F32(static_cast<double>(random.Next() % TurnSpan) + static_cast<double>(particle.rotation));
    return {};
}

// 0x542d01..0x542d5f: the 62b/0 after the prefix: the life rand() % 10 + 30, the light at 1 and the speed (0; 0;
// (rand() % 100) · 0.02f) (0x6b89e4). Two rand(). The other subtypes leave at 0x54304c with only the prefix.
void CreateTheFlareColumn(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int LifeSpan = 10, LifeBase = 30, RiseSpan = 100;
    constexpr double RiseStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au)); // 0x6b89e4: 0.02f
    if (particle.subtype != 0)
        return;
    particle.life = random.Next() % LifeSpan + LifeBase;
    particle.velocity.x = 0.0f;
    particle.velocity.y = 0.0f;
    particle.light = glm::vec3(1.0f);
    particle.velocity.z = F32(static_cast<double>(random.Next() % RiseSpan) * RiseStep);
}

// 0x546f0d..0x546f51: the 62b/0, after the common movement (with the turned speed): the scale at 0.2f, the speed's z
// plus 0.4f, up to 8 (`fcomp` with `test ah, 1`: with more than 8, or a NaN, it stays at 8); with less than 5 life,
// the light by 0.8333333f (0x547fc9). The other subtypes have nothing of their own.
void StepTheFlareColumn(Particle& particle)
{
    constexpr float ColumnScale = std::bit_cast<float>(0x3e4ccccdu);                // 0x546f21: 0.2f
    constexpr double Rise = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4: 0.4f
    constexpr double TopSpeed = 8.0;                                                // 0x6b86c4
    constexpr std::int32_t FadeLife = 5;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u)); // 0x6b8b98: 0.8333333f
    if (particle.subtype != 0)
        return;
    const double speed = static_cast<double>(particle.velocity.z) + Rise;
    particle.scale = ColumnScale;
    particle.velocity.z = speed <= TopSpeed ? F32(speed) : static_cast<float>(TopSpeed);
    if (particle.life < FadeLife)
        for (int channel = 0; channel < 3; ++channel)
            particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * Fade);
}

// 0x543bf1..0x543c25: the 6ae after the prefix, without reading the subtype: life 30, the light at 0.1f and the turn
// rand() % 360. One rand().
void CreateTheHole(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t HoleLife = 30;
    constexpr float HoleLight = std::bit_cast<float>(0x3dcccccdu); // 0x543c06: 0.1f
    particle.life = HoleLife;
    const int turn = random.Next() % FullTurn;
    particle.light = glm::vec3(HoleLight);
    particle.rotation = static_cast<float>(turn);
}

// 0x547d42..0x547dc1: the 6ae, without reading the subtype: the turn plus 5; with less than 20 life, the scale minus
// 0.08f (0x6ba1ac) and, with less than 10, the light by 0.7692308f (0x6ba240); from 20 to 29, the light by 1.1f
// (0x6b8830).
void StepTheHole(Particle& particle)
{
    constexpr double Turn = 5.0; // 0x6b87f8
    constexpr std::int32_t ShrinkLife = 20, FadeLife = 10, BirthLife = 30;
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3da3d70au)); // 0.08f
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));   // 0.7692308f
    constexpr double Glow = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu));   // 1.1f
    particle.rotation = F32(static_cast<double>(particle.rotation) + Turn);
    double factor = 1.0;
    if (particle.life < ShrinkLife)
    {
        particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
        if (particle.life >= FadeLife)
            return;
        factor = Fade;
    }
    else if (particle.life < BirthLife)
    {
        factor = Glow;
    }
    else
    {
        return;
    }
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * factor);
}

// 0x542b3b..0x542bf3, the 5e4 after the prefix: life 12, the scale ((rand() & 3) + 8) · 0.05f (0x6b8a48, on the x87),
// which overrides the argument's, the x turn rand() % 360 and the speed (0; −((rand() & 15) + 8); rand() % 6 − 3)
// turned by AngleMatrix of the requested angles (0x5fc8f0 and 0x5fcbd0, between the second rand() and the third,
// without taking any); with type 0x5e4 (0x542bdb), the light (0.1f; 0; 0). Four rand(). The and with the sign
// correction is the plain &.
constexpr std::int32_t BloodLife = 12;
void CreateTheBlood(Particle& particle, const glm::vec3& angles, Crt::CrtRandom& random)
{
    constexpr int ScaleMask = 3, ScaleBase = 8, FallMask = 15, FallBase = 8, SideSpan = 6, SideCenter = 3;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    constexpr float Red = std::bit_cast<float>(0x3dcccccdu);
    particle.life = BloodLife;
    particle.scale = F32(static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * ScaleStep);
    particle.angles.x = static_cast<float>(random.Next() % FullTurn);
    const auto matrix = Math::OriginalAngleMatrix(angles);
    const float fall = -static_cast<float>((random.Next() & FallMask) + FallBase);
    const float side = static_cast<float>((random.Next() % SideSpan) - SideCenter);
    particle.velocity = Math::OriginalVectorRotate(glm::vec3(0.0f, fall, side), matrix);
    particle.light = glm::vec3(Red, 0.0f, 0.0f);
}

// 0x546b04..0x546b61, after the common life and movement (also with the life at 0): the speed by 0.95f (0x6ba30c), the
// frame (+0x48) (12 − life) / 3 (imul by 0x55555556, toward 0) and the position plus the speed again, unturned. The x
// adds the product left on the x87 after the fst (in 53 bits, exact); the y and the z, the stored float.
void StepTheBlood(Particle& particle)
{
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3f733333u));
    constexpr std::int32_t BloodFrameSteps = 3;
    const double x = static_cast<double>(particle.velocity.x) * Brake;
    particle.velocity.x = F32(x);
    particle.velocity.y = F32(static_cast<double>(particle.velocity.y) * Brake);
    particle.velocity.z = F32(static_cast<double>(particle.velocity.z) * Brake);
    particle.field48 = std::bit_cast<float>((BloodLife - particle.life) / BloodFrameSteps);
    particle.position.x = F32(x + static_cast<double>(particle.position.x));
    particle.position.y = F32(static_cast<double>(particle.velocity.y) + static_cast<double>(particle.position.y));
    particle.position.z = F32(static_cast<double>(particle.velocity.z) + static_cast<double>(particle.position.z));
}

bool IsSwellLifeParticle(std::int32_t type, std::int32_t subtype)
{
    return type == SwellLifeParticle && subtype == SwellLifeParticleSubtype;
}

// 0x544a9d..0x544ba0: even without a valid owner and on the last step it takes its two rand().
void StepTheSwellLife(Particle& particle, Crt::CrtRandom& random, const ParticleOwnerBones& ownerBones)
{
    const auto point =
        particle.typedOwner.kind == ParticleOwnerKind::Actor && ownerBones ? ownerBones(particle) : std::nullopt;
    if (point)
        particle.position = *point;
    else
        particle.alive = false;
    constexpr double Rise = static_cast<double>(std::bit_cast<float>(0x3dc28f5cu));
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f3da12fu));
    constexpr int RiseSpan = 40, RiseBase = 60;
    constexpr int ShrinkSpan = 400, ShrinkBase = 400;
    const double speed = static_cast<double>(random.Next() % RiseSpan + RiseBase) * Rise;
    particle.verticalSpeed = F32(static_cast<double>(particle.verticalSpeed) + speed);
    const double shrink = static_cast<double>(random.Next() % ShrinkSpan + ShrinkBase) * Shrink;
    particle.scale = F32(static_cast<double>(particle.scale) - shrink);
    particle.position.z = F32(static_cast<double>(particle.position.z) + particle.verticalSpeed);
    for (int axis = 0; axis < 3; ++axis)
        particle.light[axis] = F32(static_cast<double>(particle.light[axis]) * Fade);
}

// 0x546d54..0x546d6e, after the common life and movement (also with the life at 0): the scale (+0x10)
// sin(life · [0x6ba370]) · [0x6b86b8] (fild, fmul, fsin and fmul on the x87) and 0x53fdb0, which puts the position
// back on the owner's bone.
Core::Expected<void, ParticleError> StepTheSkillShine(Particle& particle, IParticleTrigonometry* trigonometry,
                                                      const ParticleOwnerBones& ownerBones)
{
    constexpr double ShineFrequency = static_cast<double>(std::bit_cast<float>(0x3e32b8c2u)); // π/18
    constexpr float ShineSize = 3.0f;
    if (trigonometry == nullptr)
        return Core::Unexpected(ParticleError::UnsupportedType);
    const auto scale = trigonometry->SineProductPC53(static_cast<double>(particle.life) * ShineFrequency, ShineSize);
    if (!scale.HasValue())
        return Core::Unexpected(scale.Error());
    particle.scale = F32(scale.Value());
    const auto point = ownerBones ? ownerBones(particle) : std::nullopt;
    if (!point)
        return Core::Unexpected(ParticleError::MissingOwner);
    particle.position = *point;
    return {};
}

// 0x540a04..0x540b41, case 12 of the 5c6 (the twenty of the 0x815, 0x4fb0d1): texture 0x5fc, +0x38 at 1.0f, the
// position plus (0; −100; 0) turned by AngleMatrix of the requested angles (0x5fc8f0 and 0x5fcbd0) and then
// (rand() & 15) − 8 in x and y and 150 in z; the speed ((rand() % 20) · 0.1f − 1; the same; −(rand() % 3) − 3), life
// 40 and the requested scale minus (rand() % 15) · 0.1f. Each sum on the x87 before the store. Six rand().
void CreateTheBowSpark(Particle& particle, float scale, Crt::CrtRandom& random)
{
    constexpr std::int32_t BowSparkTexture = 0x5fc;
    constexpr std::int32_t BowSparkLife = 40;
    constexpr int Spread = 15, SpeedSpan = 20, FallSpan = 3, ShrinkSpan = 15;
    constexpr double Center = 8.0, Rise = 150.0, Fall = 3.0, Unit = 1.0;
    constexpr double Tenth = Math::TenthAsDouble; // 0x6b8a34
    particle.texture = BowSparkTexture;
    particle.field38 = std::bit_cast<std::uint32_t>(1.0f);
    const auto pushed =
        Math::OriginalVectorRotate(glm::vec3(0.0f, -100.0f, 0.0f), Math::OriginalAngleMatrix(particle.angles));
    for (int axis = 0; axis < 3; ++axis)
        particle.position[axis] = F32(static_cast<double>(pushed[axis]) + static_cast<double>(particle.position[axis]));
    const int x = random.Next() & Spread;
    particle.position.x = F32((static_cast<double>(x) + static_cast<double>(particle.position.x)) - Center);
    const int y = random.Next() & Spread;
    particle.position.y = F32((static_cast<double>(y) + static_cast<double>(particle.position.y)) - Center);
    particle.position.z = F32(static_cast<double>(particle.position.z) + Rise);
    const int vx = random.Next() % SpeedSpan;
    particle.velocity.x = F32((static_cast<double>(vx) * Tenth) - Unit);
    const int vy = random.Next() % SpeedSpan;
    particle.velocity.y = F32((static_cast<double>(vy) * Tenth) - Unit);
    const int vz = random.Next() % FallSpan;
    particle.life = BowSparkLife;
    particle.velocity.z = F32(-static_cast<double>(vz) - Fall);
    const int shrink = random.Next() % ShrinkSpan;
    particle.scale = F32(static_cast<double>(scale) - (static_cast<double>(shrink) * Tenth));
}

// 0x545397..0x5453ce and 0x545340..0x545363, after the common life and movement: rand() & 1 minus 0.6f in the red and
// the green and minus 0.8f in the blue, the scale minus 0.04f and +0x38 minus 0.001f; with +0x38 at 0 or less (or
// NaN), it dies. One rand().
void StepTheBowSpark(Particle& particle, Crt::CrtRandom& random)
{
    constexpr double RedGreen = static_cast<double>(std::bit_cast<float>(0x3f19999au)); // 0x6b8a20
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));     // 0x6b8a38
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d23d70au));   // 0x6b8aec
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3a83126fu));     // 0x6b89c8
    const auto flicker = static_cast<double>(random.Next() & 1);
    particle.light.x = F32(flicker - RedGreen);
    particle.light.y = particle.light.x;
    particle.light.z = F32(flicker - Blue);
    particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
    const double faded = static_cast<double>(std::bit_cast<float>(particle.field38)) - Fade;
    particle.field38 = std::bit_cast<std::uint32_t>(F32(faded));
    if (!(faded > 0.0))
        particle.alive = false;
}

// 0x540bb2..0x540c77, the table of 0x544428 by subtype: /0 scale 0.15f (0x3e19999a); /1 life 10 and scale 0; /2 life
// 20 and scale 1; /3 life 1 and the requested scale; /4 scale 0.25f; /5 the requested scale. The /4 and the /5 keep
// the requested light; the others (and the subtypes outside the table) add 0.5 to each channel. The /0 and the /4
// keep the prefix's life (2). No rand().
void CreateTheLightning(Particle& particle, const glm::vec3& light)
{
    constexpr std::int32_t HitLife = 10, FollowLife = 20, FlashLife = 1;
    constexpr double Lift = 0.5; // 0x6b882c
    switch (particle.subtype)
    {
    case LightningTrailSubtype:
        particle.scale = std::bit_cast<float>(0x3e19999au);
        break;
    case LightningHitSubtype:
        particle.life = HitLife;
        particle.scale = 0.0f;
        break;
    case LightningFollowSubtype:
        particle.life = FollowLife;
        particle.scale = 1.0f;
        break;
    case LightningFlashSubtype:
        particle.life = FlashLife;
        break;
    case LightningPlainSubtype:
        particle.scale = std::bit_cast<float>(0x3e800000u);
        return;
    case LightningPlainScaledSubtype:
        return;
    default:
        break;
    }
    for (int axis = 0; axis < 3; ++axis)
        particle.light[axis] = F32(static_cast<double>(light[axis]) + Lift);
}

// 0x545419..0x5454c8, with all subtypes, after the common life and movement: the turn (+0x44) is
// (ftol(clock) % 1000) · 0.001f and the dynamic light of radius 3 (0x5f9780) is (v · 0.5f; v; v · 0.8f) with
// v = life · 0.1f: the red from the unrounded product, the blue from the already stored green. The /2 (0x54548f) also
// grows 0.1f, takes that same light and follows its owner (+0x40 → +0x10), 80 higher. No rand().
Core::Expected<void, ParticleError> StepTheLightning(Particle& particle, const float* worldClock,
                                                     Terrain::TerrainLighting* lighting,
                                                     const ParticleOwnerStates& ownerStates)
{
    constexpr std::int32_t ClockSpan = 1000;
    constexpr std::int32_t LightningLightRadius = 3;
    constexpr double Milli = static_cast<double>(std::bit_cast<float>(0x3a83126fu));  // 0x6b89c8
    constexpr double Tenth = Math::TenthAsDouble;                                     // 0x6b8a34
    constexpr double Half = Math::HalfAsDouble;                                       // 0x6b882c
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));   // 0x6b8a38
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
    constexpr double Height = 80.0;                                                   // 0x6b9ffc
    if (worldClock == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    particle.rotation = F32(static_cast<double>(Math::FtolLow(static_cast<double>(*worldClock)) % ClockSpan) * Milli);
    const double glow = static_cast<double>(particle.life) * Tenth;
    const float green = F32(glow);
    const glm::vec3 color{F32(glow * Half), green, F32(static_cast<double>(green) * Blue)};
    if (!WorldObjects::AddDynamicLight(lighting, particle.position, color, LightningLightRadius, "0x54547b").HasValue())
        return Core::Unexpected(ParticleError::MissingDynamicLight);
    if (particle.subtype != LightningFollowSubtype)
        return {};
    particle.scale = F32(static_cast<double>(particle.scale) + Growth);
    particle.light = color;
    const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
    if (!owner)
        return Core::Unexpected(ParticleError::MissingOwner);
    particle.position.x = owner->position.x;
    particle.position.y = owner->position.y;
    particle.position.z = F32(static_cast<double>(owner->position.z) + Height);
    return {};
}

bool IsSpark(const Particle& particle)
{
    return particle.type == SparkParticle && particle.subtype == RavenSparkSubtype;
}

bool IsLoginCloud(const Particle& particle)
{
    return particle.type == CloudParticle && particle.subtype == LoginCloudSubtype;
}

// 0x54757a and 0x54768f: Crywolf's 622/9 shares the step from 0x5476a8 with the 622/8, and both fall into the turn's
// default (0x547738).
bool IsCloud8(const Particle& particle)
{
    return particle.type == CloudParticle && (particle.subtype == 8 || particle.subtype == CrywolfCloudSubtype);
}

void CreateTheCloud8(Particle& particle, float inputScale, Crt::CrtRandom& random)
{
    particle.life = 300;

    const int r1 = random.Next();
    particle.field38 = std::bit_cast<std::uint32_t>(Cloud8InitialQ);
    particle.verticalSpeed = F32(static_cast<double>(r1 % 1000));

    const int r2 = (random.Next() % 500) - 250;
    particle.position.x = F32(static_cast<double>(r2) + static_cast<double>(particle.position.x));
    const int r3 = (random.Next() % 500) - 250;
    particle.position.y = F32(static_cast<double>(r3) + static_cast<double>(particle.position.y));
    const int r4 = (random.Next() % 20) - 20;
    particle.position.z = F32(static_cast<double>(r4) + static_cast<double>(particle.position.z));

    const int r5 = random.Next() % 100;
    particle.start.z = particle.position.z;
    particle.start.y = F32(static_cast<double>(r5) * Cloud8RandomStep);
    const int r6 = (random.Next() % 20) + 180;
    particle.scale = F32(static_cast<double>(r6) * Cloud8RandomStep);
    const int r7 = random.Next() % 30;
    particle.rotationFactor = F32(static_cast<double>(r7) * Cloud8RandomStep + static_cast<double>(inputScale));

    // The body sets the three dwords back to zero after R7.
    particle.velocity = glm::vec3(0.0f);
}

// 0x54330a..0x5433f4, the 622/9 of Crywolf's 0x4d and 0x4e: like the 622/8, seven rand() in the same order, but the
// scale multiplies the argument's (+0x10, the prefix's) and +0x60 adds nothing to it.
void CreateTheCloud9(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int SpinSpan = 10;
    particle.life = 300;
    const int r1 = random.Next();
    particle.field38 = std::bit_cast<std::uint32_t>(Cloud8InitialQ);
    particle.verticalSpeed = F32(static_cast<double>(r1 % 1000));
    const int r2 = (random.Next() % 500) - 250;
    particle.position.x = F32(static_cast<double>(r2) + static_cast<double>(particle.position.x));
    const int r3 = (random.Next() % 500) - 250;
    particle.position.y = F32(static_cast<double>(r3) + static_cast<double>(particle.position.y));
    const int r4 = (random.Next() % 20) - 20;
    particle.position.z = F32(static_cast<double>(r4) + static_cast<double>(particle.position.z));
    const int r5 = random.Next() % 100;
    particle.start.z = particle.position.z;
    particle.start.y = F32(static_cast<double>(r5) * Cloud8RandomStep);
    // 0x5433bd..0x5433c9: fild, by 0.01f and by the scale at +0x10, on the x87.
    const int r6 = (random.Next() % 20) + 180;
    particle.scale = F32(static_cast<double>(r6) * Cloud8RandomStep * static_cast<double>(particle.scale));
    const int r7 = random.Next() % SpinSpan;
    particle.velocity = glm::vec3(0.0f);
    particle.rotationFactor = F32(static_cast<double>(r7) * Cloud8RandomStep);
}

void StepTheCloud8(Particle& particle)
{
    const std::int32_t life = particle.life;
    if (life <= 0)
    {
        particle.alive = false;
        return;
    }

    const float q = std::bit_cast<float>(particle.field38);
    if (life > 150)
    {
        // FCOMP also takes unordered through the sum, equivalent to q<1 or unordered.
        if (!(q >= Cloud8RiseLimit))
        {
            particle.field38 = std::bit_cast<std::uint32_t>(F32(static_cast<double>(q) + Cloud8Rise));
        }
        particle.position.z = F32(static_cast<double>(particle.position.z) + 1.0);
        return;
    }

    // The late comparison strictly requires q>0.1f; unordered keeps +0x38.
    if (q > Cloud8FallLimit)
    {
        particle.field38 = std::bit_cast<std::uint32_t>(F32(static_cast<double>(q) - Cloud8Fall));
    }
    particle.position.z = F32(static_cast<double>(particle.position.z) - 0.5);
}

// 0x53ff3a..: 5d9, 5db and 5dc go to the same case of 0x53fe70 (0x541172) and of 0x5446e0 (0x5457a5).
bool IsFireType(std::int32_t type)
{
    return type == FireParticle || type == DeathSpiritFireParticle || type == BlueTrailFireParticle;
}

bool IsFire(const Particle& particle)
{
    return IsFireType(particle.type);
}

// 0x5466c3: the table of 0x548310 sends the 5f2/2 and Crywolf's 5f2/21 to the same step.
bool IsWorldSmoke(const Particle& particle)
{
    return particle.type == BreathParticle &&
           (particle.subtype == WorldSmokeSubtype || particle.subtype == CrywolfSmokeSubtype);
}

// 0x542370..0x5423d8, Crywolf's 5f2/21: life 80, scale ((rand() & 63) + 64) · scale · 0.005f, turn rand() % 360 and
// vertical speed ((rand() & 31) + 60) · 0.1f; the and with the sign correction is the plain &.
void CreateTheCrywolfSmoke(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t Life = 80;
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8.
    constexpr int ScaleMask = 63, ScaleBase = 64, RiseMask = 31, RiseBase = 60;
    particle.life = Life;
    const int size = (random.Next() & ScaleMask) + ScaleBase;
    particle.scale = F32((static_cast<double>(size) * static_cast<double>(particle.scale)) * Step);
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    particle.verticalSpeed = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * SmokeSlowdown);
}

bool IsStoneDust(const Particle& particle)
{
    return particle.type == BreathParticle && particle.subtype == StoneDustSubtype;
}

// 0x541d81..0x541e72, the 5f2/11 (and the /14, which MuRet does not create): life 50; the x and the y plus
// (rand() & 63) − 32 and the z plus (rand() & 63) + 32, each on the x87 until its fstp; the tilt rand() % 90 − 45, the
// yaw rand() % 360 and the billboard turn rand() % 360; +0x60..+0x68 store the creation's light and the speed is
// (0, −((rand() & 7) + 40), 0), which the common movement turns by the angles. Seven rand(); the and with the sign
// correction is the plain &.
void CreateTheStoneDust(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t Life = 50;
    constexpr int OffsetMask = 63, OffsetCenter = 32, TiltSpan = 90, TiltCenter = 45, PushMask = 7, PushBase = 40;
    particle.life = Life;
    const int x = (random.Next() & OffsetMask) - OffsetCenter;
    particle.position.x = F32(static_cast<double>(x) + static_cast<double>(particle.position.x));
    const int y = (random.Next() & OffsetMask) - OffsetCenter;
    particle.position.y = F32(static_cast<double>(y) + static_cast<double>(particle.position.y));
    const int z = (random.Next() & OffsetMask) + OffsetCenter;
    particle.position.z = F32(static_cast<double>(z) + static_cast<double>(particle.position.z));
    particle.angles.x = static_cast<float>(random.Next() % TiltSpan - TiltCenter);
    particle.angles.z = static_cast<float>(random.Next() % FullTurn);
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    particle.velocity.x = 0.0f;
    particle.field64 = {particle.light.y, particle.light.z};
    particle.rotationFactor = particle.light.x;
    // fild and fchs: the negated push enters the float whole.
    particle.velocity.y = -static_cast<float>((random.Next() & PushMask) + PushBase);
    particle.velocity.z = 0.0f;
}

// 0x546111..0x54616c, after the common movement: the light is the life by 0.02f (0x6b89e4) by the creation's
// (+0x60..+0x68), each channel from the same product on the x87; the speed by 0.4f (0x6b87b4), the scale plus 0.05f
// (0x6b8a48) and the z 1 less (0x6b87d4). It does not read the owner (+0x40).
void StepTheStoneDust(Particle& particle)
{
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    constexpr double Sink = 1.0;
    const double glow = static_cast<double>(particle.life) * LightPerLife;
    particle.light = {F32(glow * static_cast<double>(particle.rotationFactor)),
                      F32(glow * static_cast<double>(particle.field64.x)),
                      F32(glow * static_cast<double>(particle.field64.y))};
    for (int axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * Brake);
    particle.scale = F32(static_cast<double>(particle.scale) + Growth);
    particle.position.z = F32(static_cast<double>(particle.position.z) - Sink);
}

bool IsRisingSmoke(const Particle& particle)
{
    return particle.type == BreathParticle && particle.subtype == CrywolfRisingSmokeSubtype;
}

// 0x541a8d..0x541b03, the 5f2/33 of Crywolf's 0x48: the argument's scale plus 0.1f (0x6b87b8) and the life
// rand() % 150 (which can be 0), the tilt rand() % 90 − 45, the turn rand() % 360 and the speed (0, −(rand() & 7), 0),
// which the prefix moves; the and with the sign correction is the plain &.
void CreateTheRisingSmoke(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int LifeSpan = 150, TiltSpan = 90, TiltCenter = 45, DriftMask = 7;
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    const int life = random.Next() % LifeSpan;
    particle.scale = F32(static_cast<double>(particle.scale) + Growth);
    particle.life = life;
    particle.angles.x = static_cast<float>(random.Next() % TiltSpan - TiltCenter);
    particle.angles.z = static_cast<float>(random.Next() % FullTurn);
    particle.velocity = glm::vec3(0.0f, -static_cast<float>(random.Next() & DriftMask), 0.0f);
}

// 0x545ffc..0x54604c: the light is the life by 0.125f (0x6ba170) in the blue and by 0.4f (0x6b87b4) in the red and the
// green, from the x87; the speed, by 0.001f (0x6b89c8); the scale adds 0.02f (0x6b8b90) and fst stores it, and the z
// adds the unrounded sum.
void StepTheRisingSmoke(Particle& particle)
{
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    constexpr double Drag = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    const double glow = static_cast<double>(particle.life) * BreathLightPerLife;
    const float dim = F32(glow * Dim);
    particle.light = glm::vec3(dim, dim, F32(glow));
    for (int axis = 0; axis < 3; ++axis)
        particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * Drag);
    const double grown = static_cast<double>(particle.scale) + Growth;
    particle.scale = F32(grown);
    particle.position.z = F32(grown + static_cast<double>(particle.position.z));
}

// 0x543152..0x543178, Crywolf's water bubble 698/0: life 32 and the scale (rand() % 170 + 1) · 0.01f, which overrides
// the argument's.
constexpr std::int32_t BubbleLife = 32;
void CreateTheBubble(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int ScaleSpan = 170;
    particle.life = BubbleLife;
    particle.scale = F32(static_cast<double>(random.Next() % ScaleSpan + 1) * BreathScaleStep);
}

// 0x54719f..0x5471b2: the frame (32 − life) / 4, with the signed division by cdq, and 3 and sar, in +0x48.
void StepTheBubble(Particle& particle)
{
    constexpr std::int32_t BubbleFrameSteps = 4;
    particle.field48 = std::bit_cast<float>(static_cast<std::int32_t>((BubbleLife - particle.life) / BubbleFrameSteps));
}

bool IsHandShine(std::int32_t type, std::int32_t subtype)
{
    return type == HandShineParticle && (subtype == 0 || subtype == HandShineSecondSubtype);
}

// 0x546cd4..0x546d4f, the 5fd that is not the /0x63: the scale is sin(life · 5°) · 5 (FST); from the /2 onward, that
// same x87 stack value by 0.75 (FSTP) and the rotation 12 less. Then 0x53fdb0 puts it back in the owner's hand
// (`hands`).
Core::Expected<void, ParticleError> StepTheHandShine(Particle& particle, IParticleTrigonometry* trigonometry,
                                                     const ParticleActorHands& hands)
{
    if (trigonometry == nullptr)
        return Core::Unexpected(ParticleError::UnsupportedType);
    const double argument = static_cast<double>(particle.life) * static_cast<double>(HandShinePulseStep);
    const auto pulse = trigonometry->SineProductPC53(argument, HandShinePulse);
    if (!pulse.HasValue())
        return Core::Unexpected(pulse.Error());
    particle.scale = F32(pulse.Value());
    if (particle.subtype >= HandShineSecondSubtype)
    {
        particle.scale = F32(pulse.Value() * HandShineSecondScale);
        particle.rotation = F32(static_cast<double>(particle.rotation) - HandShineTurn);
    }
    if (particle.typedOwner.kind != ParticleOwnerKind::Actor || !hands)
        return Core::Unexpected(ParticleError::MissingOwner);
    const std::optional<glm::vec3> hand = hands(particle.typedOwner.identity, particle.subtype);
    if (!hand)
        return Core::Unexpected(ParticleError::MissingOwner);
    particle.position = *hand;
    return {};
}

bool IsMapSpark(const Particle& particle)
{
    return particle.type == SparkParticle && particle.subtype == MapSparkSubtype;
}

bool IsDrop(const Particle& particle)
{
    return particle.type == DropParticle && particle.subtype >= 0 && particle.subtype <= LastDropSubtype;
}

std::optional<ParticleError> BindingError(std::int32_t type, std::int32_t subtype)
{
    if (IsEffectParticle(type, subtype))
        return std::nullopt;
    if (IsSwellLifeParticle(type, subtype))
        return std::nullopt;
    // 0x54109f and 0x54573f: the 5ca handles any subtype; the /1 and the /2 also have their own (0x5410d9, 0x545745).
    if (type == ThunderParticle)
        return std::nullopt;
    if (type == ShinyParticle || type == FairySparkParticle)
        return IsNoriaParticle(type, subtype) ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == Particle67 || type == Particle68)
    {
        return subtype == 0 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    }
    // 0x542d60 and 0x546fa2: the 607 from 0 to 4.
    if (type == DropParticle)
    {
        return subtype >= 0 && subtype <= LastDropSubtype ? std::nullopt
                                                          : std::optional(ParticleError::UnsupportedSubtype);
    }
    // 0x541172 and 0x5457a5: the fire handles any subtype (those outside its cases, with nothing of their own).
    if (IsFireType(type))
        return std::nullopt;
    // 0x540260 and 0x544e87: the /2, the /3 and the /4 (those of 0x1b6 and Crywolf's hand).
    if (type == MagicSparkParticle)
        return subtype >= 2 && subtype <= 4 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    // 0x540d13..0x54109e and 0x545565: the 5da handles any subtype (those outside 0..9, only with the turn).
    if (type == MissileFireParticle)
        return std::nullopt;
    if (type == BreathParticle)
    {
        return subtype == CrywolfTideSmokeSubtype || subtype == CrywolfSwingSmokeSubtype ||
                       subtype == MissileSmokeSubtype || subtype == ExplosionSmokeSubtype ||
                       subtype == ViperSmokeSubtype || subtype == BreathSubtype || subtype == WorldSmokeSubtype ||
                       subtype == CrywolfSmokeSubtype || subtype == CrywolfRisingSmokeSubtype ||
                       subtype == StoneDustSubtype
                   ? std::nullopt
                   : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == BubbleParticle)
        return subtype == 0 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    // 0x542c4b and 0x546da6: the flowers 608..60a are born without reading the subtype and their step only checks the
    // /0 and the /1; 0x542d01 and 0x546f0d: the 62b outside the /0 only has the prefix; 0x543bf1 and 0x547d42: the 6ae
    // does not read the subtype.
    if (IsFlowerType(type) || type == FlareColumnParticle || type == HoleParticle)
        return std::nullopt;
    // 0x54271f: the 5f3 and the 5f6 with the /0 (Chaos Castle's stones, 0x405630 and 0x405e00) and the /1 (the path,
    // 0x403c0c, and the +0x3e5 brake, 0x506a73).
    if (IsDustType(type))
        return subtype == 0 || subtype == 1 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == FireworkParticle)
        return subtype >= -1 && subtype <= 29 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == PoundingBallParticle)
        return subtype == 0 || subtype == 1 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == ExplosionParticle)
        return subtype >= 0 && subtype <= 2 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    // 0x542a94 and 0x545ce3: the 5ee does not read the subtype.
    if (type == BlazeParticle)
        return std::nullopt;
    // 0x543939 and 0x5478d7: the 6ad handles any subtype (those outside /0 and /1 are born with nothing of their own).
    if (type == CinderParticle)
        return std::nullopt;
    // 0x5443ae and 0x5481ab: neither does the 6eb.
    if (type == ShockWaveParticle)
        return std::nullopt;
    // 0x542b3b and 0x546b04: the 5e4 does not read the subtype.
    if (type == BloodParticle || type == SiegeBlessingParticle)
        return std::nullopt;
    // 0x540bb2 and 0x545419: the 5d5 handles any subtype (those outside the table, like the /0 without its scale).
    if (type == LightningParticle)
        return std::nullopt;
    if (type == HandShineParticle)
        return IsHandShine(type, subtype) ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == SparkParticle)
    {
        return subtype == TeleportSparkSubtype || subtype == RavenSparkSubtype || subtype == MapSparkSubtype ||
                       subtype == BowSparkSubtype || IsWeaponSpark(type, subtype) || IsFenrirSpark(type, subtype)
                   ? std::nullopt
                   : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == CloudParticle)
    {
        return subtype == LoginCloudSubtype || subtype == 8 || subtype == CrywolfCloudSubtype
                   ? std::nullopt
                   : std::optional(ParticleError::UnsupportedSubtype);
    }
    return ParticleError::UnsupportedType;
}

// 0x53fe9c..0x53ff37: interleaved copies and a reread of P, without a snapshot or a reset of the reused slot.
void Initialize(Particle& particle, std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                const glm::vec3& light, std::int32_t subtype, float scale, std::uint32_t owner)
{
    particle.subtype = subtype;
    particle.alive = true;
    particle.type = type;
    particle.texture = type;
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.position[axis] = position[axis];
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.start[axis] = position[axis];
    }
    particle.light.x = light.x;
    particle.light.y = light.y;
    const float blue = light.z;
    particle.scale = scale;
    particle.light.z = blue;
    particle.verticalSpeed = 0.0f;
    particle.field48 = 0.0f;
    particle.rotation = 0.0f;
    particle.life = DefaultLife;
    particle.owner = owner;
    particle.typedOwner = {};
    particle.moves = true;
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.angles[axis] = angles[axis];
    }
    particle.velocity = glm::vec3(0.0f);
}

// 0x54059f..0x54061c: three angles of 0..359 and VectorRotate(0,-50,0); keeps the common life (2).
void CreateTheTeleportSpark(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int FullCircle = 360;
    constexpr float Speed = -50.0f;
    for (int axis = 0; axis < 3; ++axis)
        particle.angles[axis] = static_cast<float>(random.Next() % FullCircle);
    particle.velocity =
        Math::OriginalVectorRotate(glm::vec3(0.0f, Speed, 0.0f), Math::OriginalAngleMatrix(particle.angles));
}

// 0x541172..0x54140a, the same case for 5d9, 5db and 5dc: life 24, the speed (0; ((rand() & 15) + 32) · −0.1f; 0),
// each subtype's part (read once, after the first rand(), at 0x541197), the /1's scale (0x5413c2 rereads the subtype)
// and the final turn. The subtypes without a case (2..4, 6, 8, 10 and those outside) only do the common part.
void CreateTheFire(Particle& particle, Crt::CrtRandom& random)
{
    constexpr std::int32_t ShortFireLife = 20;                                               // 0x541253
    constexpr double FountainStep = static_cast<double>(std::bit_cast<float>(0x3e19999au));  // 0x6b8ae8, 0.15f
    constexpr double TallScaleStep = static_cast<double>(std::bit_cast<float>(0x3c449ba6u)); // 0x6ba334, 0.012f
    constexpr double SwayScaleStep = static_cast<double>(std::bit_cast<float>(0x3c03126fu)); // 0x6ba330, 0.008f
    constexpr int TallScaleSpan = 16, TallScaleBase = 150, FountainSpan = 128, FountainBase = 128;
    constexpr int SwaySpan = 32, SwayCenter = 16, RingSpan = 60, RingCenter = 30;
    constexpr double RingTop = 190.0; // 0x6b8a08
    constexpr double RingSlope = 1.5; // 0x6b8b94
    const float argumentScale = particle.scale;
    particle.life = FireLife;
    particle.velocity.x = 0.0f;
    const int speed = (random.Next() % FireSpeedSpan) + FireSpeedBase;
    particle.velocity.z = 0.0f;
    const std::int32_t subtype = particle.subtype; // Relectura posterior a R1 (00541197).
    particle.velocity.y = F32(static_cast<double>(speed) * FireSpeed);
    switch (subtype)
    {
    case 0:
        particle.scale = F32(static_cast<double>((random.Next() % FireScaleSpan) + FireScaleBase) * BreathScaleStep);
        break;
    case 5:
        particle.scale =
            F32((static_cast<double>(random.Next() % FireScaleSpan + FireScaleBase) * argumentScale) * BreathScaleStep);
        break;
    case FireTallSubtype:
        // 0x54120b: the final turn overrides this rand()'s.
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        particle.scale =
            F32((static_cast<double>(random.Next() % TallScaleSpan + TallScaleBase) * argumentScale) * TallScaleStep);
        break;
    case FireFountainSubtype:
        // 0x541253..0x5412bf: life 20, rises straight and does not use the requested scale.
        particle.life = ShortFireLife;
        particle.velocity.x = 0.0f;
        particle.velocity.y = 0.0f;
        particle.velocity.z = F32(static_cast<double>(random.Next() % FountainSpan + FountainBase) * FountainStep);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        particle.scale = F32(static_cast<double>(random.Next() % TallScaleSpan + TallScaleBase) * TallScaleStep);
        break;
    case FireSwaySubtype:
    case FireLongSwaySubtype:
        // 0x5412d2 (/7) and 0x54132c (/11, which sets the life back to 24): the speed (0; ((rand() & 31) − 16) · −0.1f;
        // 0) and the scale ((rand() & 63) + 128) · 0.008f plus the requested one.
        if (subtype == FireLongSwaySubtype)
            particle.life = FireLife;
        particle.velocity.x = 0.0f;
        {
            const int sway = random.Next() % SwaySpan - SwayCenter;
            particle.velocity.z = 0.0f;
            particle.velocity.y = F32(static_cast<double>(sway) * FireSpeed);
        }
        particle.scale = F32(static_cast<double>(random.Next() % FireScaleSpan + FireScaleBase) * SwayScaleStep +
                             static_cast<double>(argumentScale));
        break;
    case FireRingSubtype:
    {
        // 0x54138a..0x5413bf: the start (+0x6c) is (r; r; 190 − |r| · 1.5) with r = rand() % 60 − 30; the step adds the
        // owner's position.
        const int offset = random.Next() % RingSpan - RingCenter;
        particle.start.x = static_cast<float>(offset);
        particle.start.y = static_cast<float>(offset);
        particle.start.z = F32(RingTop - static_cast<double>(offset < 0 ? -offset : offset) * RingSlope);
        break;
    }
    default:
        break;
    }
    if (particle.subtype == 1) // 0x5413c2 rereads the live subtype.
    {
        particle.scale =
            F32(static_cast<double>((random.Next() % FireSmallScaleSpan) + FireSmallScaleBase) * BreathScaleStep);
    }
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
}

void CreateTheWorldSmoke(Particle& particle, Crt::CrtRandom& random)
{
    particle.life = WorldSmokeLife;
    particle.scale = F32(static_cast<double>((random.Next() % FireScaleSpan) + SmokeScaleBase) * BreathScaleStep);
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    particle.verticalSpeed = F32(static_cast<double>((random.Next() % FireSpeedBase) + SmokeSpeedBase) * SmokeSlowdown);
}

// Contract 53fe70-map-variants §5f2/6..8 and Step5446e0 §5f2/4,6,7,8.
constexpr std::int32_t Map8SmokeLife = 30;
constexpr std::int32_t Map8EightLife = 24;
constexpr int Map8PhaseSpan = 1000;
constexpr int Map8ScaleSpan = 20;
constexpr int Map8SixScaleBase = 180;
constexpr int Map8SevenScaleBase = 120;
constexpr int Map8OffsetSpan = 200;
constexpr int Map8OffsetCenter = 100;
constexpr int Map8HeightBase = 20;
constexpr int Map8SevenSpeedSpan = 4;
constexpr int Map8SevenSpeedBase = 6;
constexpr int Map8EightSpeedSpan = 8;
constexpr int Map8EightSpeedBase = 32;
constexpr std::int32_t Map8FadeLife = 5;
constexpr double Map8RiseFactor = 20.0;

// Contract 53fe70-map-variants §5f2/6..8: each R comes before its own rereads.
bool IsMap8Smoke(std::int32_t type, std::int32_t subtype)
{
    return type == BreathParticle && (subtype == 4 || subtype == 6 || subtype == 7 || subtype == 8);
}

void CreateTheMap8Smoke(Particle& p, float argumentScale, Crt::CrtRandom& random)
{
    constexpr int RandomTurn = 360;
    constexpr double PhaseStep = static_cast<double>(std::bit_cast<float>(0x3ba3d70au));
    constexpr double EightSpeed = static_cast<double>(std::bit_cast<float>(0xbe99999au));
    constexpr double EightScale = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
    if (p.subtype == 6)
    {
        p.life = Map8SmokeLife;
        p.verticalSpeed = static_cast<float>(random.Next() % Map8PhaseSpan);
        p.scale = F32(static_cast<double>(random.Next() % Map8ScaleSpan + Map8SixScaleBase) * BreathScaleStep);
        const int x = random.Next() % Map8OffsetSpan - Map8OffsetCenter;
        p.position.x = F32(static_cast<double>(x) * argumentScale + p.position.x);
        const int y = random.Next() % Map8OffsetSpan - Map8OffsetCenter;
        p.position.y = F32(static_cast<double>(y) * argumentScale + p.position.y);
        const int z = random.Next() % Map8ScaleSpan + Map8HeightBase;
        p.velocity.x = 0;
        p.velocity.y = 0;
        p.velocity.z = 0;
        const double raised = static_cast<double>(z) + p.position.z;
        p.position.z = F32(raised);
        p.rotation = F32(raised);
        return;
    }
    const bool seven = p.subtype == 7;
    p.life = seven ? Map8SmokeLife : Map8EightLife;
    p.velocity.x = 0;
    const int speed = random.Next() % (seven ? Map8SevenSpeedSpan : Map8EightSpeedSpan) +
                      (seven ? Map8SevenSpeedBase : Map8EightSpeedBase);
    p.velocity.z = 0;
    p.velocity.y = F32(static_cast<double>(speed) * (seven ? static_cast<double>(p.scale) : EightSpeed));
    if (seven)
    {
        p.verticalSpeed = F32(static_cast<double>(random.Next() % Map8OffsetSpan) * PhaseStep);
        const int scale = random.Next() % Map8ScaleSpan + Map8SevenScaleBase;
        p.scale = F32((static_cast<double>(scale) * p.scale) * BreathScaleStep);
    }
    else
    {
        p.scale = F32(static_cast<double>(p.scale) * EightScale);
    }
    p.rotation = static_cast<float>(random.Next() % RandomTurn);
}

// Contract 5446e0 §5f2/4: Z uses the wide accumulator, not its f32 store.
void StepTheMap8Four(Particle& p)
{
    const double color = static_cast<double>(p.life) * BreathLightPerLife;
    p.light.x = F32(color * static_cast<double>(std::bit_cast<float>(0x3ef0f0f1u)));
    p.light.y = F32(color * static_cast<double>(std::bit_cast<float>(0x3eca3097u)));
    p.light.z = F32(color * static_cast<double>(std::bit_cast<float>(0x3ea0a0a1u)));
    const double rise = static_cast<double>(p.verticalSpeed) + BreathRise;
    p.verticalSpeed = F32(rise);
    p.position.z = F32(rise + p.position.z);
    p.scale = F32(static_cast<double>(p.scale) + BreathGrowth);
}

// Contract 5446e0 §5f2/6 and trig-design §Step5f2/6: the evaluator returns the 53-bit product of an output retained
// in 64 bits. t stays independent until FTOL.
Core::Expected<void, ParticleError> StepTheMap8Six(Particle& p, const float& worldClock,
                                                   IParticleTrigonometry& trigonometry)
{
    constexpr std::int32_t SustainedLife = 10;
    constexpr std::int32_t ScalePeriod = 1800;
    constexpr double HeightFrequency = static_cast<double>(std::bit_cast<float>(0x3951b717u));
    constexpr double ScaleFrequency = static_cast<double>(std::bit_cast<float>(0x3ae4c388u));
    constexpr float HeightMultiplier = 20.0f;
    constexpr float ScaleMultiplier = 0.5f;
    constexpr double ScaleBase = static_cast<double>(std::bit_cast<float>(0x3fe66666u));
    const double clock = static_cast<double>(worldClock);
    const double t = clock + static_cast<double>(p.verticalSpeed);
    p.life = SustainedLife;
    const double heightArgument = t * HeightFrequency;
    const auto height = trigonometry.SineProductPC53(heightArgument, HeightMultiplier);
    if (!height.HasValue())
    {
        return Core::Unexpected(height.Error());
    }
    p.position.z = F32(height.Value() + static_cast<double>(p.rotation));
    const std::int32_t low = Client::Math::FtolLow(t);
    p.light.x = std::bit_cast<float>(0x3eb851ecu);
    const std::int32_t remainder = low % ScalePeriod;
    p.light.y = std::bit_cast<float>(0x3e99999au);
    p.light.z = std::bit_cast<float>(0x3e75c290u);
    const double scaleArgument = static_cast<double>(remainder) * ScaleFrequency;
    const auto scale = trigonometry.SineProductPC53(scaleArgument, ScaleMultiplier);
    if (!scale.HasValue())
    {
        return Core::Unexpected(scale.Error());
    }
    p.scale = F32(scale.Value() + ScaleBase);
    return {};
}

void StepTheMap8Seven(Particle& p)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));
    constexpr double Fall = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    const double scale = static_cast<double>(p.scale) + Growth;
    const std::int32_t life = p.life;
    p.scale = F32(scale);
    const double fall = static_cast<double>(p.verticalSpeed) + 1.0;
    p.verticalSpeed = F32(fall);
    p.velocity.y = F32(static_cast<double>(p.velocity.y) - Fall);
    p.position.z = F32(static_cast<double>(p.position.z) - fall);
    double color = 1.0;
    if (life < Map8FadeLife)
    {
        color = static_cast<double>(life) * BreathLightPerLife;
        p.scale = F32(static_cast<double>(p.scale) - Fall);
    }
    p.light.x = F32(color * static_cast<double>(std::bit_cast<float>(0x3f39999au)));
    p.light.y = F32(color * static_cast<double>(std::bit_cast<float>(0x3f126e98u)));
    p.light.z = F32(color * static_cast<double>(std::bit_cast<float>(0x3eaa7efau)));
}

void StepTheMap8Eight(Particle& p)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
    constexpr double ColorStep = static_cast<double>(std::bit_cast<float>(0x3d2aaaabu));
    constexpr double Damping = static_cast<double>(std::bit_cast<float>(0x3ed70a3du));
    const double growth = static_cast<double>(p.verticalSpeed) + Growth;
    const std::int32_t life = p.life;
    p.verticalSpeed = F32(growth);
    if (life > Map8FadeLife)
    {
        p.scale = F32(growth + p.scale);
        p.position.z = F32(static_cast<double>(p.verticalSpeed) * Map8RiseFactor + p.position.z);
        const double color = static_cast<double>(life) * ColorStep;
        p.light.x = F32(color);
        p.light.y = F32(color);
        p.light.z = F32(color);
        p.velocity.x = F32(static_cast<double>(p.velocity.x) * Damping);
        p.velocity.y = F32(static_cast<double>(p.velocity.y) * Damping);
        p.velocity.z = F32(static_cast<double>(p.velocity.z) * Damping);
        return;
    }
    const double red = static_cast<double>(p.light.x) * 0.5;
    p.velocity.x = 0;
    p.velocity.y = 0;
    p.velocity.z = 0;
    p.light.x = F32(red);
    p.light.y = F32(static_cast<double>(p.light.y) * 0.5);
    p.light.z = F32(static_cast<double>(p.light.z) * 0.5);
}

void CreateTheParticle67(Particle& particle, Crt::CrtRandom& random)
{
    particle.life = Particle67Life;
    const int scaleSteps = (random.Next() % Particle67ScaleSpan) + Particle67ScaleBase;
    particle.scale = F32(static_cast<double>(scaleSteps) * Particle67ScaleStep);
}

// The 67's life, rand() and scale, and then one rand() per axis for the position.
void CreateTheParticle68(Particle& particle, Crt::CrtRandom& random)
{
    CreateTheParticle67(particle, random);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int offset = (random.Next() % Particle68OffsetSpan) - Particle68OffsetCenter;
        particle.position[axis] = F32(static_cast<double>(offset) + static_cast<double>(particle.position[axis]));
    }
}

std::optional<ParticleError> StepTheParticle67Or68(Particle& particle, const std::int32_t* map)
{
    if (particle.life <= 0)
    {
        particle.alive = false;
    }
    particle.scale = particle.subtype == 1
                         ? F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.scale))
                         : F32(static_cast<double>(particle.scale) + Particle67Growth);
    // 0x4303c0 (contract leaf-4303c0) rereads the map's raw32 DWORD here, not the low byte nor a map captured on entry,
    // and returns AL=1 only for 34.
    if (map == nullptr)
    {
        return ParticleError::MissingMap;
    }
    if (*map == Particle67ColorMap)
    {
        particle.light.z = Particle67Blue;
        particle.light.x = 1.0f;
        particle.light.y = 1.0f;
        return std::nullopt;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.light[axis] = F32(static_cast<double>(particle.light[axis]) - Particle67Fade);
    }
    return std::nullopt;
}

void CreateTheMapSpark(Particle& particle, Crt::CrtRandom& random)
{
    particle.life = MapSparkLife;
    particle.angles.x = 0.0f;
    const int yaw = random.Next() % FullTurn;
    particle.angles.z = 0.0f;
    particle.angles.y = static_cast<float>(yaw);
    const auto matrix = Math::OriginalAngleMatrix(particle.angles);
    glm::vec3 offset(0.0f);
    offset.z = F32(static_cast<double>(random.Next() % MapSparkOffsetSpan) + MapSparkOffsetBase);
    particle.velocity = Math::OriginalVectorRotate(offset, matrix);
    // 0x5408c8/d7/e8 replace the speed after loading each sum, before storing the position.
    const double x = static_cast<double>(particle.position.x) + static_cast<double>(particle.velocity.x);
    particle.velocity.x = MapSparkForward;
    particle.position.x = F32(x);
    const double y = static_cast<double>(particle.position.y) + static_cast<double>(particle.velocity.y);
    particle.velocity.y = 0.0f;
    particle.position.y = F32(y);
    const double z = static_cast<double>(particle.velocity.z) + static_cast<double>(particle.position.z);
    particle.velocity.z = MapSparkDown;
    particle.position.z = F32(z);
}

void StepTheMapSpark(Particle& particle)
{
    const double speed = static_cast<double>(particle.velocity.x) - MapSparkBrake;
    const std::int32_t life = particle.life;
    particle.velocity.x = F32(speed);
    particle.velocity.z = F32(static_cast<double>(particle.velocity.z) - MapSparkFall);
    const double scale = static_cast<double>(particle.scale) - MapSparkShrink;
    particle.scale = F32(scale);
    if (life <= 0)
    {
        particle.alive = false;
    }
    // TEST AH,1 checks C0: less and unordered turn it off, using the ST0 from before the store32.
    if (scale < MapSparkMinimum || std::isnan(scale))
    {
        particle.alive = false;
    }
}

// 0x542d75..0x542ecf: the life rand() % 10 + 30, the light 1 (overrides the requested one) and the scale
// (rand() % 6 + 4) · 0.03f (0x6b8bd4), two rand(); the /2 also asks for sound 0x1d8 (0x542e08, whoever creates it
// asks). The /3 (0x542e1a) adds that scale to the requested one and rolls another one the same way for the rise
// (+0x50): three rand().
void CreateTheDrop(Particle& particle, Crt::CrtRandom& random)
{
    particle.life = DropLifeBase + (random.Next() % DropLifeSpan);
    const int scaleSteps = (random.Next() % DropScaleSpan) + DropScaleBase;
    if (particle.subtype == RisingDropSubtype)
    {
        particle.scale = F32(static_cast<double>(scaleSteps) * DropScaleStep + static_cast<double>(particle.scale));
        const int riseSteps = (random.Next() % DropScaleSpan) + DropScaleBase;
        particle.light = glm::vec3(1.0f);
        particle.verticalSpeed = F32(static_cast<double>(riseSteps) * DropScaleStep);
        return;
    }
    particle.light = glm::vec3(1.0f);
    particle.scale = F32(static_cast<double>(scaleSteps) * DropScaleStep);
}

// 0x546fb2..0x547024 (/0) and 0x5470d3..0x547145 (/3): the frame +0x48 (an integer) goes up 1 and the position walks
// (rand() % 20 − 10; rand() % 20 − 10; rand() % 20 + 10) by the scale (the /3, by its rise +0x50) and by 2.5.
void WalkTheDrop(Particle& particle, Crt::CrtRandom& random, float stride)
{
    const std::uint32_t frame = std::bit_cast<std::uint32_t>(particle.field48) + 1u;
    particle.field48 = std::bit_cast<float>(frame);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int direction = axis == 2 ? DropWalkCenter : -DropWalkCenter;
        const int steps = (random.Next() % DropWalkSpan) + direction;
        // The scale is reread after each RNG; there is no store32 between the two products.
        particle.position[axis] = F32(static_cast<double>(steps) * static_cast<double>(stride) * DropWalkFactor +
                                      static_cast<double>(particle.position[axis]));
    }
}

// 0x5470a0..0x5470ce (/2) and 0x54714a..0x54719a (/4): the z rises (rand() % 20 + 10) · 0.3f and the scale grows
// 0.005f (0x6b89d8); the /4, with the stored z above 350 (0x6b89ac; neither less, nor equal, nor NaN), leaves it at
// 350 and the life at 0.
void FloatTheDrop(Particle& particle, Crt::CrtRandom& random)
{
    constexpr double Lift = static_cast<double>(std::bit_cast<float>(0x3e99999au));  // 0x6b89dc, 0.3f
    constexpr double Swell = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8, 0.005f
    constexpr float Ceiling = 350.0f;                                                // 0x6b89ac
    const int steps = (random.Next() % DropWalkSpan) + DropWalkCenter;
    particle.position.z = F32(static_cast<double>(steps) * Lift + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + Swell);
    if (particle.subtype == CappedDropSubtype && particle.position.z > Ceiling)
    {
        particle.position.z = Ceiling;
        particle.life = 0;
    }
}

// 0x547029..0x54709b, the /1: the frame goes up 1, the x and the y walk (rand() % 30 − 15) by the scale, the z rises
// (rand() % 20 + 10) by the scale and by 0.3f, and the scale grows 0.002f (0x6b89bc).
void BubbleTheDrop(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int BubbleSpan = 30, BubbleCenter = 15;
    constexpr double Lift = static_cast<double>(std::bit_cast<float>(0x3e99999au));  // 0x6b89dc, 0.3f
    constexpr double Swell = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6b89bc, 0.002f
    const std::uint32_t frame = std::bit_cast<std::uint32_t>(particle.field48) + 1u;
    particle.field48 = std::bit_cast<float>(frame);
    for (int axis = 0; axis < 2; ++axis)
    {
        const int steps = (random.Next() % BubbleSpan) - BubbleCenter;
        particle.position[axis] = F32(static_cast<double>(steps) * static_cast<double>(particle.scale) +
                                      static_cast<double>(particle.position[axis]));
    }
    const int steps = (random.Next() % DropWalkSpan) + DropWalkCenter;
    particle.position.z = F32(static_cast<double>(steps) * static_cast<double>(particle.scale) * Lift +
                              static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + Swell);
}

void StepTheDrop(Particle& particle, Crt::CrtRandom& random)
{
    switch (particle.subtype)
    {
    case BubbleDropSubtype:
        BubbleTheDrop(particle, random);
        return;
    case SoundingDropSubtype:
    case CappedDropSubtype:
        FloatTheDrop(particle, random);
        return;
    case RisingDropSubtype:
        WalkTheDrop(particle, random, particle.verticalSpeed);
        return;
    default:
        WalkTheDrop(particle, random, particle.scale);
        return;
    }
}

std::int32_t WrapSubtract(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

// 0x5457a5..0x545982, the step of 5d9, 5db and 5dc: the rise (+0x50) + 0.004f, the subtype's case (the table of
// 0x548270 for 0..13; the others, that of 1..4) and the common part: z + rise · 10 and the frame (+0x48)
// (23 − life) / 6.
Core::Expected<void, ParticleError> StepTheFire(Particle& particle, Crt::CrtRandom& random,
                                                const ParticleOwnerStates& ownerStates)
{
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3d2aaaabu));       // 0x6ba3c4
    constexpr double Damp = static_cast<double>(std::bit_cast<float>(0x3f733333u));       // 0x6ba30c, 0.95f
    constexpr double Turn = 5.0;                                                          // 0x6b87f8
    constexpr double RingShrink = static_cast<double>(std::bit_cast<float>(0x3c360b61u)); // 0x6ba3c0
    constexpr int TurnSpan = 10, TurnBase = 10, RingSpan = 60, RingDivisor = 100;
    constexpr std::int32_t SwayLife = 15, SwaySteps = 6, LongSwayLife = 24, LongSwaySteps = 3, LongSwayStart = 12;
    const float rise = F32(static_cast<double>(particle.verticalSpeed) + FireRise);
    particle.verticalSpeed = rise;
    const float light = F32(static_cast<double>(particle.life) * Fade);
    switch (particle.subtype)
    {
    case 0:
    case FireSwaySubtype:
        // 0x545894 (/0) and 0x54587c (/7, which first leaves a frame that 0x54597f overrides: it is not seen).
        if (particle.subtype == FireSwaySubtype)
            particle.field48 = std::bit_cast<float>(WrapSubtract(SwayLife, particle.life) / SwaySteps);
        particle.scale = F32(static_cast<double>(particle.scale) - FireShrink);
        break;
    case 5:
    case 6:
        // 0x545842: the scale drops 0.04f and the turn adds 5, without braking the speed.
        particle.scale = F32(static_cast<double>(particle.scale) - FireShrink);
        particle.rotation = F32(static_cast<double>(particle.rotation) + Turn);
        break;
    case 8:
        // 0x54585f: does not brake the speed nor add the rise to the scale.
        particle.scale = F32(static_cast<double>(particle.scale) * Damp);
        particle.rotation = F32(static_cast<double>(particle.rotation) + Turn);
        break;
    case FireRingSubtype:
    {
        // 0x5458d3..0x545926: the position is the owner's plus the start; the rise adds (rand() % 60 + 60) / 100,
        // integer, and the scale drops the unrounded sum by 0.011111111f.
        const auto owner = ownerStates ? ownerStates(particle.typedOwner) : std::nullopt;
        if (!owner)
            return Core::Unexpected(ParticleError::MissingOwner);
        for (int axis = 0; axis < 3; ++axis)
            particle.position[axis] =
                F32(static_cast<double>(owner->position[axis]) + static_cast<double>(particle.start[axis]));
        const int step = (random.Next() % RingSpan + RingSpan) / RingDivisor;
        const double sum = static_cast<double>(step) + static_cast<double>(particle.verticalSpeed);
        particle.verticalSpeed = F32(sum);
        particle.scale = F32(static_cast<double>(particle.scale) - sum * RingShrink);
        break;
    }
    case 10:
        particle.scale = F32(static_cast<double>(particle.scale) * Damp); // 0x545831
        break;
    case FireLongSwaySubtype:
        // 0x5458a5: the /11's frame, which 0x54597f overrides (it is not seen), and the scale grows 0.04f.
        if (particle.life > LongSwayStart)
            particle.field48 = std::bit_cast<float>(WrapSubtract(LongSwayLife, particle.life) / LongSwaySteps);
        particle.scale = F32(static_cast<double>(particle.scale) + FireShrink);
        break;
    case FireTallSubtype:
    case FireFountainSubtype:
    {
        // 0x5457dc and 0x5457f1: also on the last step, after lowering the life and the common movement.
        const double turn = static_cast<double>(random.Next() % TurnSpan) + TurnBase;
        particle.light = glm::vec3(light);
        particle.rotation = F32(turn + static_cast<double>(particle.rotation));
        particle.scale = F32(static_cast<double>(particle.scale) - FireShrink);
        break;
    }
    default:
        // 0x54592b: 1..4, those greater than 13 and the negative ones.
        particle.scale = F32(static_cast<double>(rise) + static_cast<double>(particle.scale));
        for (int axis = 0; axis < 3; ++axis)
            particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * FireDamping);
        break;
    }
    const std::int32_t frame = WrapSubtract(FireLife - 1, particle.life) / FrameSteps;
    particle.position.z =
        F32(static_cast<double>(particle.verticalSpeed) * FireHeightFactor + static_cast<double>(particle.position.z));
    particle.field48 = std::bit_cast<float>(frame);
    return {};
}

void StepTheWorldSmoke(Particle& particle)
{
    particle.light = glm::vec3(F32(static_cast<double>(particle.life) * SmokeFade));
    // FST +0x50 does not round ST0: X and Z use the double accumulator retained in this step.
    const double speed = static_cast<double>(particle.verticalSpeed) - SmokeSlowdown;
    particle.verticalSpeed = F32(speed);
    particle.position.x = F32(static_cast<double>(particle.position.x) - speed * SmokeDrift);
    particle.position.z = F32(speed + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) - BreathScaleStep);
}

void CreateTheCloud(Particle& particle, Crt::CrtRandom& random)
{
    particle.life = CloudLife;
    particle.light = glm::vec3(0.0f);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int offset = (random.Next() % CloudOffsetSpan) - CloudOffsetCenter;
        particle.position[axis] = F32(static_cast<double>(particle.position[axis]) + offset);
    }
    particle.start = particle.position;
    const int scaleSteps = (random.Next() % CloudScaleSpan) + CloudScaleBase;
    particle.scale = F32(static_cast<double>(scaleSteps) * CloudScaleStep + static_cast<double>(particle.scale));
    const int speedSteps = (random.Next() % CloudSpeedSpan) + CloudSpeedBase;
    const float speed = F32(static_cast<double>(speedSteps) * CloudSpeedStep);
    particle.velocity = glm::vec3(speed, speed, 0.0f);
}

void GrowTheCloudLight(Particle& particle)
{
    const double red = static_cast<double>(particle.light.x) + CloudGrowth;
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.light[axis] = F32(static_cast<double>(particle.light[axis]) + CloudGrowth);
    }
    // 0x547639: compares before reloading the stored red; a NaN does not reach the threshold.
    if (red >= static_cast<double>(CloudLightLimit))
    {
        particle.light = glm::vec3(CloudLightLimit);
    }
}

void StepTheCloud(Particle& particle)
{
    particle.rotationFactor = CloudRotationFactor;
    // 0x5475be: X is not reloaded after the fst; Y and Z are used already stored in float.
    const double x = static_cast<double>(particle.velocity.x) + static_cast<double>(particle.position.x);
    particle.position.x = F32(x);
    particle.position.y = F32(static_cast<double>(particle.velocity.y) + static_cast<double>(particle.position.y));
    particle.position.z = F32(static_cast<double>(particle.velocity.z) + static_cast<double>(particle.position.z));
    const double dx = x - static_cast<double>(particle.start.x);
    const double dy = static_cast<double>(particle.position.y) - static_cast<double>(particle.start.y);
    const double dz = static_cast<double>(particle.position.z) - static_cast<double>(particle.start.z);
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    // 0x547602: unordered takes the same branch as <= 300 because of the x87 flags.
    if (distance <= CloudGrowthRadius || std::isnan(distance))
    {
        GrowTheCloudLight(particle);
        return;
    }
    if (distance > CloudDeathRadius)
    {
        particle.alive = false;
        return;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.light[axis] = F32(static_cast<double>(particle.light[axis]) * CloudFade);
    }
}

// 0x545209 → 0x545f9e: the spark shrinks by 0.9 (0x6b8a1c) and fades by 1/1.03 (0x6ba3b0), each product stored in a
// float.
void StepTheSpark(Particle& particle)
{
    particle.scale = F32(static_cast<double>(particle.scale) * SparkShrink);
    for (int k = 0; k < 3; ++k)
    {
        particle.light[k] = F32(static_cast<double>(particle.light[k]) * SparkFade);
    }
}
constexpr std::int32_t OrbitSubtype = 3;
constexpr float OrbitPhase = 0.1f; // 006b87b8 bits3dcccccd.
constexpr float OrbitRadius = 40.0f;
constexpr float OrbitShrink = 0.002f;            // 006b89bc bits3b03126f.
constexpr float OrbitFade = 0.9090908765792847f; // 006ba26c bits3f68ba2e.
constexpr std::int32_t OrbitFadeLife = 20;
constexpr std::int32_t OrbitContinuation = 10;
constexpr std::int32_t OrbitSpeedSpan = 100;
constexpr std::int32_t OrbitPhaseSpan = 300;
constexpr std::int32_t OrbitPhaseBias = 150;
constexpr std::int32_t OrbitScaleSpan = 6;
constexpr float OrbitUnit = 0.01f; // 006b86c0/006b88d4 bits3c23d70a.
constexpr float OrbitRiseRange = 4.0f;
// The 60f/4 of Blood Castle's torch: 0x542f2e leaves the life at 40 and rises with 5 (0x6b87f8); 0x5472e2 shrinks
// 0.004f (0x6b89e8) and fades with the life at 30 or less.
constexpr std::int32_t TorchOrbitSubtype = 4;
constexpr std::int32_t TorchOrbitLife = 40;
constexpr float TorchOrbitRiseRange = 5.0f;
constexpr float TorchOrbitShrink = 0.004f; // 006b89e8 bits3b83126f.
constexpr std::int32_t TorchOrbitFadeLife = 30;

// 0x542f74 and 0x5471bd: the /0 is born and turns with the same instructions as the /3 (and as the /6, not ported).
constexpr std::int32_t SetOrbitSubtype = 0;
// 0x547380: the /1 the /0 turns into. 0x53fe70 does not create it; its step only fades (0x5472da).
constexpr std::int32_t FallenOrbitSubtype = 1;
constexpr std::int32_t FallenOrbitLife = 20;

bool IsOrbitSubtype(std::int32_t subtype)
{
    return subtype == SetOrbitSubtype || subtype == OrbitSubtype || subtype == TorchOrbitSubtype ||
           subtype == AnchoredOrbitSubtype || subtype == 5;
}

bool IsSteppedOrbitSubtype(std::int32_t subtype)
{
    return IsOrbitSubtype(subtype) || subtype == FallenOrbitSubtype;
}

// The owner's (+0x40) action (+0x111) that 0x54735f reads: that of the pool's character or, when the owner is an
// effect with a model (the 0x61d, 0x52b8ff and 0x52b977), that of the effect table's slot, alive or not, like the byte
// the original reads through the pointer. nullopt if the owner is neither or the resolver is missing.
std::optional<std::uint8_t> SetOrbitOwnerAction(const ParticleOwner& owner, const ParticleActorActions& actorActions,
                                                const ParticleOwnerStates& ownerStates)
{
    if (owner.kind == ParticleOwnerKind::Actor)
        return actorActions ? actorActions(owner.identity) : std::nullopt;
    if (owner.kind != ParticleOwnerKind::ModelEffect || !ownerStates)
        return std::nullopt;
    const std::optional<ParticleOwnerState> state = ownerStates(owner);
    return state ? std::optional<std::uint8_t>(state->action) : std::nullopt;
}

// 0x547354..0x5473a9: the /0 reads the owner's (+0x40) action (+0x111). In [0xd, 0x21] or [0x38, 0x3c] it becomes
// the /1, with the life at 20 if it had more, the speed (+0x54, where the phase was) at 0 and the start at the
// position.
Core::Expected<void, ParticleError> FallTheSetOrbit(Particle& particle, const ParticleActorActions& actorActions,
                                                    const ParticleOwnerStates& ownerStates)
{
    const std::optional<std::uint8_t> action = SetOrbitOwnerAction(particle.typedOwner, actorActions, ownerStates);
    if (!action)
        return Core::Unexpected(ParticleError::MissingOwner);
    constexpr std::uint8_t FirstFall = 0xd, LastFall = 0x21, SecondFirstFall = 0x38, SecondLastFall = 0x3c;
    const bool falls =
        (*action >= FirstFall && *action <= LastFall) || (*action >= SecondFirstFall && *action <= SecondLastFall);
    if (!falls)
        return {};
    particle.subtype = FallenOrbitSubtype;
    if (particle.life > FallenOrbitLife)
        particle.life = FallenOrbitLife;
    particle.velocity = glm::vec3(0.0f);
    particle.start = particle.position;
    return {};
}

double OrbitArgument(const Particle& particle)
{
    const double phase = static_cast<double>(particle.life) + static_cast<double>(particle.velocity.x);
    return phase * static_cast<double>(OrbitPhase);
}

Core::Expected<void, ParticleError> ContinueOrbitCreation(Particle& particle, float temporaryX, double retainedY,
                                                          IParticleTrigonometry& trigonometry)
{
    const double argument = OrbitArgument(particle);
    const auto sine = trigonometry.SineProductPC53(argument, OrbitRadius);
    if (!sine.HasValue())
    {
        return Core::Unexpected(sine.Error());
    }
    particle.start.x = F32(sine.Value() + static_cast<double>(temporaryX));
    const auto cosine = trigonometry.CosineProductPC53(argument, OrbitRadius);
    if (!cosine.HasValue())
    {
        return Core::Unexpected(cosine.Error());
    }
    particle.start.y = F32(retainedY - cosine.Value());
    return {};
}

// Create53fe70 §60f/3: four R, rereads after R4 and the late continuation 10.
Core::Expected<void, ParticleError> CreateTheOrbit(Particle& particle, float inputScale, Crt::CrtRandom& random,
                                                   IParticleTrigonometry& trigonometry)
{
    const bool torch = particle.subtype == TorchOrbitSubtype;
    const bool falling = particle.subtype == 5;
    particle.life = torch || falling ? TorchOrbitLife : OrbitLife;
    const int rise = random.Next() % OrbitSpeedSpan;
    const double riseUnit = static_cast<double>(rise) * static_cast<double>(OrbitUnit);
    const double riseRange = riseUnit * static_cast<double>(torch || falling ? TorchOrbitRiseRange : OrbitRiseRange);
    particle.verticalSpeed = F32(riseRange + 1.0);
    particle.velocity.x = static_cast<float>((random.Next() % OrbitPhaseSpan) - OrbitPhaseBias);
    const int scaleStep = random.Next() % OrbitScaleSpan;
    const double scaleDelta = static_cast<double>(scaleStep) * static_cast<double>(OrbitUnit);
    particle.scale = F32(scaleDelta + static_cast<double>(inputScale));
    const int rotation = random.Next() % FullTurn;
    particle.angles.x = 0.0f;
    particle.angles.y = 0.0f;
    particle.angles.z = 0.0f;
    const bool continuation = particle.subtype == OrbitContinuation;
    particle.start.z = particle.position.z;
    particle.rotation = static_cast<float>(rotation);
    const double retainedX = particle.position.x;
    const float temporaryX = F32(retainedX);
    particle.start.x = F32(retainedX);
    const double retainedY = particle.position.y;
    particle.start.y = F32(retainedY);
    if (!continuation)
    {
        return {};
    }
    return ContinueOrbitCreation(particle, temporaryX, retainedY, trigonometry);
}

// Step5446e0 §60f/3: the selectors read after the helper; no new clock loads.
Core::Expected<void, ParticleError> StepTheOrbit(Particle& particle, IParticleTrigonometry& trigonometry,
                                                 const ParticleActorActions& actorActions,
                                                 const ParticleOwnerStates& ownerStates)
{
    if (particle.subtype == FallenOrbitSubtype)
    {
        // 0x5472da..0x547351: without moving, the light by 0.9090909f with the life at 20 or less.
        if (particle.life <= OrbitFadeLife)
            for (int axis = 0; axis < 3; ++axis)
                particle.light[axis] = F32(static_cast<double>(particle.light[axis]) * static_cast<double>(OrbitFade));
        return {};
    }
    if (particle.subtype == AnchoredOrbitSubtype)
    {
        // 0x547286..0x5472a5: the /10 computes the argument and drops it (fstp st(0)); the x and the y go back to the
        // start.
        particle.position.x = particle.start.x;
        particle.position.y = particle.start.y;
        particle.position.z =
            F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.position.z));
        particle.scale = F32(static_cast<double>(particle.scale) - static_cast<double>(OrbitShrink));
        if (particle.life <= OrbitFadeLife)
        {
            for (int axis = 0; axis < 3; ++axis)
                particle.light[axis] = F32(static_cast<double>(particle.light[axis]) * static_cast<double>(OrbitFade));
        }
        return {};
    }
    const double argument = OrbitArgument(particle);
    const auto sine = trigonometry.SineProductPC53(argument, OrbitRadius);
    if (!sine.HasValue())
    {
        return Core::Unexpected(sine.Error());
    }
    particle.position.x = F32(sine.Value() + static_cast<double>(particle.start.x));
    const auto cosine = trigonometry.CosineProductPC53(argument, OrbitRadius);
    if (!cosine.HasValue())
    {
        return Core::Unexpected(cosine.Error());
    }
    particle.position.y = F32(static_cast<double>(particle.start.y) - cosine.Value());
    const double rise = particle.verticalSpeed;
    const bool falling = particle.subtype == 5;
    particle.position.z = F32((falling ? -rise : rise) + static_cast<double>(particle.position.z));
    if (falling)
        particle.start.x = F32(static_cast<double>(particle.start.x) + 2.5);
    const bool torch = particle.subtype == TorchOrbitSubtype;
    particle.scale =
        F32(static_cast<double>(particle.scale) - static_cast<double>(torch ? TorchOrbitShrink : OrbitShrink));
    if (particle.life <= (torch ? TorchOrbitFadeLife : OrbitFadeLife))
    {
        particle.light.x = F32(static_cast<double>(particle.light.x) * static_cast<double>(OrbitFade));
        particle.light.y = F32(static_cast<double>(particle.light.y) * static_cast<double>(OrbitFade));
        particle.light.z = F32(static_cast<double>(particle.light.z) * static_cast<double>(OrbitFade));
    }
    if (particle.subtype == SetOrbitSubtype)
        return FallTheSetOrbit(particle, actorActions, ownerStates);
    return {};
}
} // namespace

std::optional<std::size_t> Particles::Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                             const glm::vec3& light, std::int32_t subtype, float scale,
                                             float worldClock, Crt::CrtRandom& random)
{
    auto created = CreateChecked(type, position, angles, light, subtype, scale, worldClock, random);
    return created.HasValue() ? created.Value() : std::nullopt;
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                         std::int32_t subtype, float scale, float worldClock, Crt::CrtRandom& random,
                         std::uint32_t owner)
{
    return CreateImpl(type, position, angles, light, subtype, scale, worldClock, nullptr, random, owner, nullptr);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                         std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                         Crt::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry& trigonometry,
                         ParticleOwner typedOwner)
{
    return CreateImpl(type, position, angles, light, subtype, scale, 0.0f, &worldClock.get(), random, owner,
                      &trigonometry, std::nullopt, typedOwner);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateKalimaChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                               const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                               Crt::CrtRandom& random, std::int32_t scene, ParticleOwner owner)
{
    if (!IsKalimaParticle(type, subtype))
    {
        if (IsParticlePoolFull(m_particles))
            return std::optional<std::size_t>{};
        return Core::Unexpected(ParticleError::UnsupportedSubtype);
    }
    return CreateImpl(type, position, angles, light, subtype, scale, worldClock, nullptr, random, 0, nullptr, scene,
                      owner);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateOwnedChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                              const glm::vec3& light, std::int32_t subtype, float scale, float worldClock,
                              Crt::CrtRandom& random, std::int32_t scene, ParticleOwner owner)
{
    if (!IsKalimaParticle(type, subtype) && !IsSelectionParticle(type, subtype) && !IsTrialsParticle(type, subtype) &&
        !IsHandShine(type, subtype) && !IsSwellLifeParticle(type, subtype) &&
        !(FollowsItsOwner(type, subtype) && !NeedsOwnerPosition(type, subtype)))
    {
        if (IsParticlePoolFull(m_particles))
            return std::optional<std::size_t>{};
        return Core::Unexpected(ParticleError::UnsupportedSubtype);
    }
    return CreateImpl(type, position, angles, light, subtype, scale, worldClock, nullptr, random, 0, nullptr, scene,
                      owner);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateTowardChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                               const glm::vec3& light, std::int32_t subtype, float scale,
                               std::reference_wrapper<const float> worldClock, Crt::CrtRandom& random,
                               IParticleTrigonometry& trigonometry, ParticleOwner owner, const glm::vec3& ownerPosition)
{
    return CreateImpl(type, position, angles, light, subtype, scale, 0.0f, &worldClock.get(), random, 0, &trigonometry,
                      std::nullopt, owner, &ownerPosition);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateOnGroundChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                 const glm::vec3& light, std::int32_t subtype, float scale,
                                 std::reference_wrapper<const float> worldClock, Crt::CrtRandom& random,
                                 IParticleTrigonometry& trigonometry, const ParticleTerrainHeight& terrainHeight)
{
    return CreateImpl(type, position, angles, light, subtype, scale, 0.0f, &worldClock.get(), random, 0, &trigonometry,
                      std::nullopt, {}, nullptr, &terrainHeight);
}

std::optional<std::size_t> Particles::CreatePiercingSpark(const glm::vec3& position, float scale, ParticleOwner owner,
                                                          const glm::vec3& center)
{
    constexpr std::int32_t PiercingLife = 15;
    constexpr double Approach = static_cast<double>(0.1f);
    for (std::size_t index = 0; index < m_particles.size(); ++index)
    {
        auto& particle = m_particles[index];
        if (particle.alive)
            continue;
        Initialize(particle, SparkParticle, position, {}, glm::vec3(1), PiercingSparkSubtype, scale, 0);
        particle.typedOwner = owner;
        particle.life = PiercingLife;
        for (int axis = 0; axis < 3; ++axis)
            particle.velocity[axis] = F32((static_cast<double>(center[axis]) - position[axis]) * Approach);
        return index;
    }
    return std::nullopt;
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateFollowingChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                  const glm::vec3& light, std::int32_t subtype, float scale, ParticleOwner owner,
                                  const ParticleOwnerBones& bones)
{
    constexpr std::int32_t SkillShineLife = 0x12; // 0x542c00
    for (std::size_t index = 0; index < m_particles.size(); ++index)
    {
        Particle& particle = m_particles[index];
        if (particle.alive)
            continue;
        if (type != SkillShineParticle)
            return Core::Unexpected(ParticleError::UnsupportedType);
        Initialize(particle, type, position, angles, light, subtype, scale, 0);
        particle.typedOwner = owner;
        particle.life = SkillShineLife;
        // 0x542c07: 0x53fdb0.
        const auto point = bones ? bones(particle) : std::nullopt;
        if (!point)
        {
            particle.alive = false;
            return Core::Unexpected(ParticleError::MissingOwner);
        }
        particle.position = *point;
        return std::optional(index);
    }
    return std::optional<std::size_t>{};
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateImpl(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                      std::int32_t subtype, float scale, float legacyClock, const float* worldClock,
                      Crt::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry* trigonometry,
                      std::optional<std::int32_t> scene, ParticleOwner typedOwner, const glm::vec3* ownerPosition,
                      const ParticleTerrainHeight* terrainHeight)
{
    for (std::size_t index = 0; index < m_particles.size(); ++index)
    {
        Particle& particle = m_particles[index];
        if (particle.alive)
        {
            continue;
        }
        const bool kalima = IsKalimaParticle(type, subtype);
        const bool selection = IsSelectionParticle(type, subtype);
        const bool trials = IsTrialsParticle(type, subtype);
        const bool bloodCastle = IsBloodCastleParticle(type, subtype);
        const bool orbit = trigonometry != nullptr && type == OrbitParticle;
        if (orbit && !IsOrbitSubtype(subtype))
        {
            return Core::Unexpected(ParticleError::UnsupportedSubtype);
        }
        // The 5fd without a character owner: the step would have nowhere to take the hand from (0x53fdb0).
        if (type == HandShineParticle && typedOwner.kind != ParticleOwnerKind::Actor)
        {
            return Core::Unexpected(ParticleError::MissingOwner);
        }
        // 5c6/4, 5de/4 and 5c5/5 read the owner's position at birth (0x540686, 0x5417ba and 0x5403ad): without it there
        // is nowhere to read it from.
        if (ownerPosition == nullptr && NeedsOwnerPosition(type, subtype))
        {
            return Core::Unexpected(ParticleError::MissingOwner);
        }
        // The mist 5f2/28..30 asks for the terrain height at birth (0x5f93c0): without it there is nowhere to read it
        // from.
        if ((terrainHeight == nullptr || !*terrainHeight) && NeedsTerrainHeight(type, subtype))
        {
            return Core::Unexpected(ParticleError::MissingTerrainHeight);
        }
        if (!kalima && !selection && !trials && !bloodCastle && !orbit &&
            !(worldClock != nullptr && IsMap8Smoke(type, subtype)))
        {
            if (const auto error = BindingError(type, subtype))
            {
                return Core::Unexpected(*error);
            }
        }
        Initialize(particle, type, position, angles, light, subtype, scale, owner);
        if (typedOwner.kind == ParticleOwnerKind::ModelEffect || FollowsItsOwner(type, subtype))
            particle.typedOwner = typedOwner;
        if (kalima)
        {
            particle.typedOwner = typedOwner;
            const auto created = CreateKalimaParticle(particle, random, scene);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return std::optional(index);
        }
        if (selection)
        {
            particle.typedOwner = typedOwner;
            CreateSelectionParticle(particle, scale, random);
            return std::optional(index);
        }
        if (trials)
        {
            particle.typedOwner = typedOwner;
            CreateTrialsParticle(particle, random);
            return std::optional(index);
        }
        if (bloodCastle)
        {
            CreateBloodCastleParticle(particle, random);
            return std::optional(index);
        }
        if (IsMissileFireKernel(type, subtype))
        {
            CreateMissileFire(particle, random);
            return std::optional(index);
        }
        if (IsEffectParticle(type, subtype))
        {
            const auto created = CreateEffectParticle(
                particle, random, {worldClock, trigonometry, ownerPosition, legacyClock, terrainHeight});
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return std::optional(index);
        }
        if (IsSwellLifeParticle(type, subtype))
        {
            // 0x540112..0x54012a: the scale argument is the bone index in +0x44; no rand().
            constexpr std::int32_t Life = 10;
            constexpr float Scale = 2;
            particle.typedOwner = typedOwner;
            particle.life = Life;
            particle.verticalSpeed = 0;
            particle.rotation = scale;
            particle.scale = Scale;
            return std::optional(index);
        }
        if (type == HandShineParticle)
        {
            // 0x542b0e..0x542b28: no rand() or clock. The position from 0x53fdb0 is set by whoever creates it, who has
            // the characters.
            particle.typedOwner = typedOwner;
            particle.life = HandShineLife;
            particle.angles.x = HandShineTilt;
            return std::optional(index);
        }
        if (type == SiegeBlessingParticle)
        {
            // 0x53fe70, case 0x638: life 20 and the scale argument, X/Y with rand%30−15, Z+240. Any subtype.
            constexpr int Jitter = 30;
            constexpr float HalfJitter = 15.0f;
            constexpr float Height = 240.0f;
            particle.life = 20;
            particle.scale = scale;
            particle.position.x = F32((static_cast<double>(random.Next() % Jitter) - HalfJitter) + particle.position.x);
            particle.position.y = F32((static_cast<double>(random.Next() % Jitter) - HalfJitter) + particle.position.y);
            particle.position.z = F32(static_cast<double>(particle.position.z) + Height);
            return std::optional(index);
        }
        if (type == ThunderParticle)
        {
            // 0x54109f: the scale ((rand() & 7) + 6) · scale · 0.1f, the turn +0x50 at 20 and +0x44 rand() % 360. The
            // subtypes that are not the /1 or the /2 keep the prefix's life 2, light and angles.
            constexpr int ScaleSpan = 8;
            constexpr int ScaleBase = 6;
            constexpr float ScaleStep = 0.1f;
            constexpr float RotationStep = 20;
            particle.scale = F32((static_cast<double>(random.Next() % ScaleSpan + ScaleBase) * scale) * ScaleStep);
            const int rotation = random.Next() % FullTurn;
            particle.verticalSpeed = RotationStep;
            particle.rotation = static_cast<float>(rotation);
            if (subtype == ThunderGraySubtype)
            {
                // 0x5410ea: life 10 and the light (0.5; 0.5; 0.5).
                constexpr std::int32_t GrayLife = 10;
                particle.life = GrayLife;
                particle.light = glm::vec3(0.5f);
            }
            else if (subtype == ThunderBoltSubtype)
            {
                // 0x541112..0x541167: texture 0x61e, life 10, a third rand() for the scale ((rand() & 7) + 6) · scale ·
                // 0.1f + 0.3f, and +0x44 and +0x50 at 0. Copies the slot's light to the caller's vector (0x541154),
                // which is the one the prefix gave it: it changes nothing.
                constexpr std::int32_t BoltTexture = 0x61e;
                constexpr std::int32_t BoltLife = 10;
                constexpr double BoltBase = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc
                particle.texture = BoltTexture;
                particle.life = BoltLife;
                const int size = random.Next() % ScaleSpan + ScaleBase;
                particle.scale = F32((static_cast<double>(size) * scale) * static_cast<double>(ScaleStep) + BoltBase);
                particle.rotation = 0.0f;
                particle.verticalSpeed = 0.0f;
            }
            return std::optional(index);
        }
        if (orbit)
        {
            particle.typedOwner = typedOwner;
            const auto created = CreateTheOrbit(particle, scale, random, *trigonometry);
            if (!created.HasValue())
            {
                return Core::Unexpected(created.Error());
            }
            return std::optional(index);
        }
        if (worldClock != nullptr && IsMap8Smoke(type, subtype) && subtype != 4)
        {
            CreateTheMap8Smoke(particle, scale, random);
            return std::optional(index);
        }
        if (IsNoriaParticle(type, subtype))
        {
            const auto created = CreateNoriaParticle(particle, scale, random, ownerPosition);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return std::optional(index);
        }
        if (type == Particle67)
        {
            CreateTheParticle67(particle, random);
            return std::optional(index);
        }
        if (type == DropParticle)
        {
            CreateTheDrop(particle, random);
            return std::optional(index);
        }
        if (IsFlowerType(type))
        {
            CreateTheFlower(particle, random);
            return std::optional(index);
        }
        if (type == FlareColumnParticle)
        {
            CreateTheFlareColumn(particle, random);
            return std::optional(index);
        }
        if (type == HoleParticle)
        {
            CreateTheHole(particle, random);
            return std::optional(index);
        }
        if (type == FireParticle || type == DeathSpiritFireParticle || type == BlueTrailFireParticle)
        {
            CreateTheFire(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == WorldSmokeSubtype)
        {
            CreateTheWorldSmoke(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == CrywolfSmokeSubtype)
        {
            CreateTheCrywolfSmoke(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == CrywolfRisingSmokeSubtype)
        {
            CreateTheRisingSmoke(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == StoneDustSubtype)
        {
            CreateTheStoneDust(particle, random);
            return std::optional(index);
        }
        if (type == CloudParticle && subtype == CrywolfCloudSubtype)
        {
            CreateTheCloud9(particle, random);
            return std::optional(index);
        }
        if (type == BubbleParticle)
        {
            CreateTheBubble(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == CrywolfTideSmokeSubtype)
        {
            // 0x5424bf: the same three-CRT sequence as 640/0, with life 25 and texture 5f2.
            particle.life = 25;
            particle.scale = F32(static_cast<double>((random.Next() & 63) + 64) * static_cast<double>(0.01f));
            particle.rotation = static_cast<float>(random.Next() % 360);
            particle.verticalSpeed = F32(static_cast<double>((random.Next() & 31) + 60) * static_cast<double>(0.1f));
            return std::optional(index);
        }
        if (type == MagicSparkParticle)
        {
            CreateTheMagicSpark(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == CrywolfSwingSmokeSubtype)
        {
            particle.life = 30;
            particle.texture = 0x6b1;
            particle.rotation = static_cast<float>(random.Next() % 360);
            return std::optional(index);
        }
        if (type == MissileFireParticle)
        {
            // 0x540f7b..0x54108a: /8 rolls six times and /9 three; the final turn overrides the previous one.
            particle.life = 32;
            if (subtype == 8)
            {
                for (int axis = 0; axis < 3; ++axis)
                    particle.position[axis] = F32(static_cast<double>(random.Next() % 20 - 10) +
                                                  static_cast<double>(particle.position[axis]));
            }
            else
                particle.texture = 0x5ae;
            particle.rotation = static_cast<float>(random.Next() % 360);
            particle.verticalSpeed =
                F32(static_cast<double>(random.Next() % 5 + 5) * static_cast<double>(subtype == 8 ? 0.2f : 0.1f));
            particle.rotation = static_cast<float>(random.Next() % 360);
            return std::optional(index);
        }
        // 0x541a36..0x541a8c: the /34 and the /35, life 30, the light (1; 0.2f; 0.2f) and the turn rand() % 360; the
        // /35 drops 5 in y and the /34 stays still (0x5435bb).
        if (type == BreathParticle &&
            (subtype == MissileSmokeSubtype || subtype == ExplosionSmokeSubtype || subtype == ViperSmokeSubtype))
        {
            particle.life = subtype == MissileSmokeSubtype ? 35 : 30;
            particle.rotation = static_cast<float>(random.Next() % 360);
            if (subtype == MissileSmokeSubtype)
                particle.texture = 0x6f1;
            else
            {
                particle.light = {1, 0.2f, 0.2f};
                particle.velocity = {0, subtype == ExplosionSmokeSubtype ? -5.0f : 0.0f, 0};
            }
            return std::optional(index);
        }
        if (type == FireworkParticle)
        {
            particle.angles = {};
            particle.rotation = static_cast<float>(random.Next() % 360);
            if (subtype == -1)
            {
                particle.life = 2;
                particle.scale = 0.25f;
                return std::optional(index);
            }
            const int divisor = subtype + 15;
            const int half = divisor / 2;
            for (int axis = 0; axis < 3; ++axis)
                particle.velocity[axis] =
                    F32(static_cast<double>(random.Next() % divisor - half) * static_cast<double>(0.15f));
            particle.velocity.z = F32(static_cast<double>(particle.velocity.z) + 10.0);
            particle.life = 50;
            particle.scale = 0.1f;
            return std::optional(index);
        }
        if (type == PoundingBallParticle)
        {
            // 0x54308a: the third roll still happens even though its vertical speed is not used in /1.
            particle.life = 30;
            particle.scale = F32(static_cast<double>((random.Next() & 63) + 64) * static_cast<double>(0.01f));
            particle.rotation = static_cast<float>(random.Next() % 360);
            particle.verticalSpeed = F32(static_cast<double>((random.Next() & 31) + 60) * static_cast<double>(0.1f));
            if (subtype == 1)
            {
                particle.texture = 0x6ad;
                particle.position.z = F32(static_cast<double>(particle.position.z) - 45.0);
            }
            return std::optional(index);
        }
        if (IsDustType(type))
        {
            CreateTheDust(particle, angles, scale, random);
            return std::optional(index);
        }
        if (type == ExplosionParticle)
        {
            CreateTheExplosion(particle, scale);
            return std::optional(index);
        }
        if (type == CinderParticle)
        {
            CreateTheCinder(particle, random);
            return std::optional(index);
        }
        if (type == ShockWaveParticle)
        {
            // 0x5443b2: life 7, no rand().
            constexpr std::int32_t ShockWaveLife = 7;
            particle.life = ShockWaveLife;
            return std::optional(index);
        }
        if (type == BlazeParticle)
        {
            // 0x542a94..0x542aa4: life 12 and the requested scale again in +0x10. The sound of 0x542aa7 is played by
            // whoever creates it.
            constexpr std::int32_t BlazeLife = 12;
            particle.life = BlazeLife;
            particle.scale = scale;
            return std::optional(index);
        }
        if (type == BloodParticle)
        {
            CreateTheBlood(particle, angles, random);
            return std::optional(index);
        }
        if (type == CloudParticle && subtype == 8)
        {
            CreateTheCloud8(particle, scale, random);
            return std::optional(index);
        }
        if (type == Particle68)
        {
            CreateTheParticle68(particle, random);
            return std::optional(index);
        }
        if (type == CloudParticle)
        {
            CreateTheCloud(particle, random);
            return std::optional(index);
        }
        if (type == SparkParticle && subtype == TeleportSparkSubtype)
        {
            CreateTheTeleportSpark(particle, random);
            return std::optional(index);
        }
        if (type == SparkParticle && subtype == MapSparkSubtype)
        {
            CreateTheMapSpark(particle, random);
            return std::optional(index);
        }
        if (type == SparkParticle && subtype == BowSparkSubtype)
        {
            CreateTheBowSpark(particle, scale, random);
            return std::optional(index);
        }
        if (type == LightningParticle)
        {
            CreateTheLightning(particle, light);
            return std::optional(index);
        }
        if (IsWeaponSpark(type, subtype))
        {
            CreateWeaponSpark(particle, random);
            return std::optional(index);
        }
        if (IsFenrirSpark(type, subtype))
        {
            CreateFenrirSpark(particle, random);
            return std::optional(index);
        }
        if (type == SparkParticle)
        {
            // 0x5406e5: life 10, the angles (0, 0, yaw) and the speed, the push (−(rand() % 2 + 2), 0, 0) (the and
            // 0x80000001 idiom, fild, fadd 2 of 0x6b8818 and fchs) turned by AngleMatrix of those angles.
            particle.life = SparkLife;
            particle.angles = glm::vec3(0.0f, 0.0f, angles.z);
            const glm::vec3 push(-(static_cast<float>(random.Next() % 2) + SparkPush), 0.0f, 0.0f);
            particle.velocity = Math::OriginalVectorRotate(push, Math::OriginalAngleMatrix(particle.angles));
            return std::optional(index);
        }
        // 0x54184b: the breath. The rand() % 32 goes with the and 0x8000001f idiom.
        particle.life = BreathLife;
        particle.scale = F32(static_cast<double>((random.Next() % 32) + BreathScaleBase) * BreathScaleStep);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(
            Client::Math::FtolLow(static_cast<double>(worldClock != nullptr ? *worldClock : legacyClock)) % FullTurn);
        return std::optional(index);
    }
    return std::optional<std::size_t>{};
}

void Particles::Clear()
{
    for (Particle& particle : m_particles)
    {
        particle.alive = false;
    }
}

std::span<Particle> Particles::All()
{
    return m_particles;
}

std::span<const Particle> Particles::All() const
{
    return m_particles;
}

bool Particles::Any() const
{
    for (const Particle& particle : m_particles)
    {
        if (particle.alive)
        {
            return true;
        }
    }
    return false;
}

ParticleWind& Particles::Wind()
{
    return m_wind;
}

const ParticleWind& Particles::Wind() const
{
    return m_wind;
}

namespace
{
Core::Expected<void, ParticleError> StepParticlesImpl(
    Particles& particles, Crt::CrtRandom& random, const float* worldClock, const std::int32_t* map,
    IParticleTrigonometry* trigonometry, const ParticleTerrainHeight& terrainHeight, const ParticleObjectOwners& owners,
    const ParticleActorActions& actorActions, const ParticleTextureHeight& textureHeight,
    Terrain::TerrainLighting* lighting, const ParticleActorHands& actorHands, const ParticleOwnerBones& ownerBones,
    const std::function<void(Audio::SoundId)>& sound, const ParticleOwnerStates& ownerStates, FrameSprites* sprites)
{
    // The private pointers only tell legacy apart from the required public API. The live overload always provides the
    // clock and the evaluator; there is no optional public service.
    StepTheWind(particles.Wind(), random);
    for (Particle& particle : particles.All())
    {
        if (!particle.alive)
        {
            continue;
        }
        particle.life = WrapSubtract(particle.life, 1);
        if (particle.life <= 0)
        {
            particle.alive = false;
        }
        if (particle.moves)
        {
            Move(particle);
        }
        if (IsSwellLifeParticle(particle.type, particle.subtype))
        {
            StepTheSwellLife(particle, random, ownerBones);
            continue;
        }
        if (IsKalimaParticle(particle.type, particle.subtype))
        {
            const auto stepped =
                StepKalimaParticle(particle, random, worldClock, trigonometry, particles.Wind().wind, ownerStates);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        if (IsSelectionParticle(particle.type, particle.subtype))
        {
            const auto stepped = StepSelectionParticle(particle, worldClock, trigonometry, owners);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        if (IsTrialsParticle(particle.type, particle.subtype))
        {
            const auto stepped = StepTrialsParticle(particle, worldClock);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        if (IsBloodCastleParticle(particle.type, particle.subtype))
        {
            StepBloodCastleParticle(particle, random);
            continue;
        }
        if (IsMissileFireKernel(particle.type, particle.subtype))
        {
            const auto stepped = StepMissileFire(particle, trigonometry);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        if (IsEffectParticle(particle.type, particle.subtype))
        {
            const auto stepped = StepEffectParticle(particle, random, trigonometry, ownerStates);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        if (trigonometry != nullptr && particle.type == OrbitParticle)
        {
            if (!IsSteppedOrbitSubtype(particle.subtype))
            {
                return Core::Unexpected(ParticleError::UnsupportedSubtype);
            }
            const auto stepped = StepTheOrbit(particle, *trigonometry, actorActions, ownerStates);
            if (!stepped.HasValue())
            {
                return stepped;
            }
            continue;
        }
        if (particle.type == SkillShineParticle)
        {
            const auto stepped = StepTheSkillShine(particle, trigonometry, ownerBones);
            if (!stepped.HasValue())
                return stepped;
            continue;
        }
        const bool map8 = worldClock != nullptr && IsMap8Smoke(particle.type, particle.subtype);
        if (map8)
        {
            if (particle.subtype == 4)
                StepTheMap8Four(particle);
            else if (particle.subtype == 6)
            {
                const auto stepped = StepTheMap8Six(particle, *worldClock, *trigonometry);
                if (!stepped.HasValue())
                {
                    return Core::Unexpected(stepped.Error());
                }
            }
            else if (particle.subtype == 7)
                StepTheMap8Seven(particle);
            else
                StepTheMap8Eight(particle);
            continue;
        }
        if (particle.type != Particle67 && particle.type != Particle68 && !IsCloud8(particle) &&
            !(particle.type == SparkParticle && particle.subtype == PiercingSparkSubtype))
        {
            if (const auto error = BindingError(particle.type, particle.subtype))
            {
                return Core::Unexpected(*error);
            }
        }
        if (particle.type == SiegeBlessingParticle)
        {
            constexpr double Shrink = static_cast<double>(0.01f);
            constexpr double Fade = static_cast<double>(0.05f);
            constexpr float Rise = 2.0f;
            particle.scale = F32(static_cast<double>(particle.scale) - Shrink);
            particle.position.x = F32(static_cast<double>(particle.position.x) + (random.Next() & 1) - 1);
            particle.position.y = F32(static_cast<double>(particle.position.y) + (random.Next() & 1) - 1);
            particle.position.z = F32(static_cast<double>(particle.position.z) + Rise);
            particle.light = glm::vec3(F32(static_cast<double>(particle.life) * Fade));
        }
        else if (particle.type == ThunderParticle)
        {
            // 0x54573f: runs even after the last common movement with alive=0.
            particle.rotation = F32(static_cast<double>(particle.verticalSpeed) + particle.rotation);
            if (particle.subtype == ThunderGraySubtype)
            {
                // 0x545750..0x54576b: the light life · 0.0666667f (0x6ba1e0) and the scale + 0.1f.
                constexpr double GrayFade = static_cast<double>(std::bit_cast<float>(0x3d888889u));
                constexpr double GrayGrowth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8
                particle.light = glm::vec3(F32(static_cast<double>(particle.life) * GrayFade));
                particle.scale = F32(static_cast<double>(particle.scale) + GrayGrowth);
            }
            else if (particle.subtype == ThunderBoltSubtype)
            {
                // 0x54577c..0x54579d: the light drops 0.01f (0x6b88d4) per channel.
                constexpr double BoltFade = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
                for (int channel = 0; channel < 3; ++channel)
                    particle.light[channel] = F32(static_cast<double>(particle.light[channel]) - BoltFade);
            }
        }
        else if (IsFallingShine(particle.type, particle.subtype))
        {
            const auto stepped = StepTheFallingShine(particle, random, sprites);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (IsNoriaParticle(particle.type, particle.subtype))
        {
            const auto stepped = StepNoriaParticle(particle, terrainHeight, ownerStates);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (particle.type == Particle67 || particle.type == Particle68)
        {
            if (const auto error = StepTheParticle67Or68(particle, map))
            {
                return Core::Unexpected(*error);
            }
        }
        else if (IsCloud8(particle))
        {
            StepTheCloud8(particle);
        }
        else if (particle.type == BubbleParticle)
        {
            StepTheBubble(particle);
        }
        else if (IsDrop(particle))
        {
            StepTheDrop(particle, random);
        }
        else if (IsFire(particle))
        {
            const auto stepped = StepTheFire(particle, random, ownerStates);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (IsWorldSmoke(particle))
        {
            StepTheWorldSmoke(particle);
        }
        else if (IsRisingSmoke(particle))
        {
            StepTheRisingSmoke(particle);
        }
        else if (IsStoneDust(particle))
        {
            StepTheStoneDust(particle);
        }
        else if (IsBreath(particle))
        {
            StepTheBreath(particle);
        }
        else if (particle.type == BreathParticle && particle.subtype == CrywolfTideSmokeSubtype)
        {
            particle.light = glm::vec3(F32(static_cast<double>(particle.life) * static_cast<double>(0.05f)));
            particle.scale = F32(static_cast<double>(particle.scale) + static_cast<double>(0.09f));
        }
        else if (particle.type == MagicSparkParticle)
        {
            StepTheMagicSpark(particle);
        }
        else if (particle.type == BreathParticle && particle.subtype == CrywolfSwingSmokeSubtype)
        {
            for (int channel = 0; channel < 3; ++channel)
                particle.light[channel] = F32(static_cast<double>(particle.light[channel]) *
                                              static_cast<double>(std::bit_cast<float>(0x3f6b851fu)));
            particle.scale = F32(static_cast<double>(particle.scale) - static_cast<double>(0.05f));
        }
        else if (particle.type == MissileFireParticle)
        {
            particle.scale = F32(static_cast<double>(particle.scale) + static_cast<double>(0.13f));
            particle.rotation =
                F32(static_cast<double>(particle.rotation) + static_cast<double>(particle.verticalSpeed));
            constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f628c76u));
            for (int channel = 0; channel < 3; ++channel)
                particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * Fade);
        }
        else if (particle.type == BreathParticle &&
                 (particle.subtype == MissileSmokeSubtype || particle.subtype == ExplosionSmokeSubtype ||
                  particle.subtype == ViperSmokeSubtype))
        {
            const float growth = particle.subtype == MissileSmokeSubtype ? 0.09f : std::bit_cast<float>(0x3d75c28fu);
            if (particle.subtype == MissileSmokeSubtype)
                particle.scale = F32(static_cast<double>(particle.scale) + static_cast<double>(growth));
            constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f762763u));
            for (int channel = 0; channel < 3; ++channel)
                particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * Fade);
            if (particle.subtype != MissileSmokeSubtype)
                particle.scale = F32(static_cast<double>(particle.scale) + static_cast<double>(growth));
        }
        else if (particle.type == FireworkParticle)
        {
            particle.velocity.z = F32(static_cast<double>(particle.velocity.z) - 0.5);
        }
        else if (particle.type == PoundingBallParticle)
        {
            particle.light = glm::vec3(F32(static_cast<double>(particle.life) * static_cast<double>(0.05f)));
            particle.scale =
                F32(static_cast<double>(particle.scale) + static_cast<double>(particle.subtype == 1 ? 0.19f : 0.05f));
            if (particle.subtype == 1)
                particle.position.z = F32(static_cast<double>(particle.position.z) + 5.0);
        }
        else if (IsDust(particle))
        {
            const auto stepped = StepTheDust(particle, terrainHeight, textureHeight);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (IsFlowerType(particle.type))
        {
            const auto stepped = StepTheFlower(particle, random, terrainHeight);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (particle.type == FlareColumnParticle)
        {
            StepTheFlareColumn(particle);
        }
        else if (particle.type == HoleParticle)
        {
            StepTheHole(particle);
        }
        else if (particle.type == ExplosionParticle)
        {
            const auto stepped = StepTheExplosion(particle, particles, random, worldClock, lighting, sound);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (particle.type == CinderParticle)
        {
            StepTheCinder(particle);
        }
        else if (particle.type == ShockWaveParticle)
        {
            // 0x5481ab..0x5481d8: the light by 0.5555556f (0x6ba234) and the scale plus 0.3f (0x6b89dc).
            for (int channel = 0; channel < 3; ++channel)
                particle.light[channel] = F32(static_cast<double>(particle.light[channel]) *
                                              static_cast<double>(std::bit_cast<float>(0x3f0e38e4u)));
            particle.scale =
                F32(static_cast<double>(particle.scale) + static_cast<double>(std::bit_cast<float>(0x3e99999au)));
        }
        else if (particle.type == BlazeParticle)
        {
            // 0x545ce3..0x545cfb: the frame (+0x48) is (12 − life) / 3, the quotient of imul by 0x55555556 (toward 0).
            constexpr std::int32_t BlazeLife = 12, BlazeFrameLength = 3;
            particle.field48 = std::bit_cast<float>((BlazeLife - particle.life) / BlazeFrameLength);
        }
        else if (particle.type == BloodParticle)
        {
            StepTheBlood(particle);
        }
        else if (particle.type == HandShineParticle)
        {
            const auto stepped = StepTheHandShine(particle, trigonometry, actorHands);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (IsMapSpark(particle))
        {
            StepTheMapSpark(particle);
        }
        else if (particle.type == SparkParticle && particle.subtype == PiercingSparkSubtype)
        {
            // 0x5450ce: the stored start rules even after the common movement and death.
            constexpr std::int32_t DetachedLife = 5;
            constexpr double Brightness = static_cast<double>(0.2f);
            constexpr double Growth = static_cast<double>(0.05f);
            if (particle.life > DetachedLife)
            {
                if (!ownerBones)
                    return Core::Unexpected(ParticleError::MissingOwner);
                if (!ownerBones(particle))
                    particle.life = DetachedLife;
            }
            particle.light = glm::vec3(F32(static_cast<double>(particle.life) * Brightness));
            particle.scale = F32(static_cast<double>(particle.scale) + Growth);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float position = F32(static_cast<double>(particle.start[axis]) + particle.velocity[axis]);
                particle.position[axis] = position;
                particle.start[axis] = position;
            }
        }
        else if (particle.type == SparkParticle && particle.subtype == BowSparkSubtype)
        {
            StepTheBowSpark(particle, random);
        }
        else if (particle.type == LightningParticle)
        {
            const auto stepped = StepTheLightning(particle, worldClock, lighting, ownerStates);
            if (!stepped.HasValue())
                return stepped;
        }
        else if (particle.type == SparkParticle && particle.subtype == TeleportSparkSubtype)
        {
            // 0x5450a9: subtracts 2 and dies under 0.2 (also unordered); it does not change the light.
            constexpr double Shrink = 2.0;
            constexpr double Minimum = static_cast<double>(0.2f);
            const double changed = static_cast<double>(particle.scale) - Shrink;
            particle.scale = F32(changed);
            if (!(changed >= Minimum))
                particle.alive = false;
        }
        else if (IsSpark(particle))
        {
            StepTheSpark(particle);
        }
        else if (IsWeaponSpark(particle.type, particle.subtype))
        {
            StepWeaponSpark(particle);
        }
        else if (IsFenrirSpark(particle.type, particle.subtype))
        {
            StepFenrirSpark(particle);
        }
        else if (IsLoginCloud(particle))
        {
            StepTheCloud(particle);
        }
    }
    return {};
}

} // namespace

Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles, Crt::CrtRandom& random,
                                                         const std::int32_t* map,
                                                         const ParticleTerrainHeight& terrainHeight)
{
    return StepParticlesImpl(particles, random, nullptr, map, nullptr, terrainHeight, {}, {}, {}, nullptr, {}, {}, {},
                             {}, nullptr);
}

Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight, const ParticleObjectOwners& owners,
                     const ParticleActorActions& actorActions, const ParticleTextureHeight& textureHeight,
                     Terrain::TerrainLighting* lighting, const ParticleActorHands& actorHands,
                     const ParticleOwnerBones& ownerBones)
{
    return StepParticlesImpl(particles, random, &worldClock.get(), map, &trigonometry, terrainHeight, owners,
                             actorActions, textureHeight, lighting, actorHands, ownerBones, {}, {}, nullptr);
}

Core::Expected<void, ParticleError>
StepParticlesChecked(Particles& particles, Crt::CrtRandom& random, std::reference_wrapper<const float> worldClock,
                     const std::int32_t* map, IParticleTrigonometry& trigonometry,
                     const ParticleTerrainHeight& terrainHeight, const ParticleObjectOwners& owners,
                     const ParticleActorActions& actorActions, const ParticleTextureHeight& textureHeight,
                     Terrain::TerrainLighting* lighting, const ParticleActorHands& actorHands,
                     const ParticleOwnerBones& ownerBones, const std::function<void(Audio::SoundId)>& sound,
                     const ParticleOwnerStates& ownerStates, FrameSprites* sprites)
{
    return StepParticlesImpl(particles, random, &worldClock.get(), map, &trigonometry, terrainHeight, owners,
                             actorActions, textureHeight, lighting, actorHands, ownerBones, sound, ownerStates,
                             sprites);
}

std::optional<ParticleError> StepParticles(Particles& particles, Crt::CrtRandom& random)
{
    const auto stepped = StepParticlesChecked(particles, random);
    return stepped.HasValue() ? std::nullopt : std::optional(stepped.Error());
}
} // namespace Client::Particles
