#include "Particles/KalimaParticleKernels.h"
#include "Core/Expected.h"
#include "Math/X87.h"

#include "Crt/CrtRandom.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/Ftol.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Client::Particles
{
namespace
{
// binary32 constants of the ASM 0x543b82..0x544260 and 0x544912..0x5481a9.
constexpr double Tenth = Math::TenthAsDouble, Hundredth = Math::HundredthAsDouble;
constexpr double SmallGrowth = static_cast<double>(0.005f), MistGrowth = static_cast<double>(0.03f);
constexpr double FantaShrink = static_cast<double>(0.02f), FantaLight = static_cast<double>(0.04f);
constexpr double Damping = static_cast<double>(0.95f), Rise = 2.5;
constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
constexpr double MistFade = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));
constexpr double Brighten = static_cast<double>(1.1f);
// 0x6ba3e8: bytes 18 b7 d1 39; keep the binary32 pattern of the drift 0x54492b/0x544950.
constexpr double WalkFactor = static_cast<double>(std::bit_cast<float>(0x39d1b718u));
constexpr int FullTurn = Math::FullTurn, LifeSpan = 10, LongLife = 30, ShortLife = 20;
constexpr int FlareZero = 0, FlareTrail = 1, FlareSix = 6, FlareSeven = 7, FlareNine = 9, SelectionScene = 4;
// The 5ac/5 of the skull 0x628/5 (0x52b843) and of the push of skill 0x28 (0x4fbd3a), and the 5ac/8 of the 0x61e/6
// (0x52baf5).
constexpr int FlareFive = 5, FlareEight = 8;
constexpr std::int32_t WaterfallSplash = WaterfallSplashFirst;
using Math::F32;
void FadeLight(Particle& p, double factor)
{
    for (int axis = 0; axis < 3; ++axis)
        p.light[axis] = F32(static_cast<double>(p.light[axis]) * factor);
}
void Jitter(Particle& p, Crt::CrtRandom& random, int xySpan, int zSpan)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const int span = axis == 2 ? zSpan : xySpan;
        const int offset = random.Next() % span - span / 2;
        p.position[axis] = F32(static_cast<double>(offset) + p.position[axis]);
    }
}
// 0x540151..0x54018f: the 5ac/0 (and the /2 and the /8, which 0x53ffa5 and 0x53ffae send to the same 0x540151) are
// the same instructions as the /6 (0x54003c); the /7 (0x53ffd9) lives 30 more and its scale is rand() % 15 + 30.
void CreateFlare(Particle& p, Crt::CrtRandom& random)
{
    const bool six = p.subtype != FlareSeven;
    p.life = random.Next() % LifeSpan + (six ? LifeSpan : LongLife);
    p.verticalSpeed = static_cast<float>(random.Next() % LifeSpan + LifeSpan);
    const int size = random.Next() % (six ? 50 : 15) + (six ? 50 : LongLife);
    p.scale = F32((static_cast<double>(size) * p.scale) * Hundredth);
}
// 0x543f04..0x543fae and 0x54417f..0x544149: 6f0/2 and 6f1/2 add the argument's scale (the prefix's, still in +0x10)
// and 6f1/2 lives 40; 6f2,6f3/4 rolls the turn and the scale before its life, speed and second scale.
constexpr std::int32_t ObjectSubtype = 2, SplashObjectSubtype = 4;
// 0x544088..0x544153 and 0x5480ae..0x5480f0: the 6f1/3 of the full set's boots (0x50ff37).
constexpr std::int32_t BootsMistSubtype = 3;
// 0x544055..0x544085 and 0x548082..0x5480ab: the 6f1/1 of the arms and the head of the 0x1b7 (0x427b8b, 0x427c42,
// 0x427cfd).
constexpr std::int32_t ArmMistSubtype = 1;
constexpr double KundunFallTurn = 4.0; // 0x6b8a44
// 0x544095..0x54414c: after the mist part, the texture cra_04 (0x5ae), the argument's light (it overrides the
// scene's), life 12, another turn, vz 2, the scale (rand() % 3 + 3) · 0.18f and another shift of ±10, ±10 and ±20.
// Five more rand(): eleven in total.
void CreateBootsMist(Particle& p, Crt::CrtRandom& random, const glm::vec3& lightArgument)
{
    constexpr std::int32_t BootsTexture = 0x5ae;
    constexpr std::int32_t BootsLife = 12;
    constexpr float BootsRise = 2.0f;
    constexpr int ScaleSpan = 3;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3e3851ecu)); // 0x6ba2d0, 0.18f
    p.texture = BootsTexture;
    p.light = lightArgument;
    p.life = BootsLife;
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.velocity.z = BootsRise;
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan + ScaleSpan) * ScaleStep);
    Jitter(p, random, ShortLife, 40);
}
Core::Expected<void, ParticleError> CreateWaterfall(Particle& p, Crt::CrtRandom& random,
                                                    std::optional<std::int32_t> scene)
{
    if (p.type == KalimaWaterfallLast && p.subtype == 1)
    {
        constexpr double BirthHeight = 50;
        p.life = ShortLife;
        p.rotation = static_cast<float>(random.Next() % FullTurn);
        p.scale = 1.0f;
        p.position.z = F32(static_cast<double>(p.position.z) + BirthHeight);
        return {};
    }
    const float scaleArgument = p.scale;
    const glm::vec3 lightArgument = p.light;
    p.life = LongLife;
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    if (p.type == KalimaWaterfallMist)
    {
        constexpr int MistSpan = 6, FallSpan = 3, ObjectMistLife = 40;
        if (p.subtype == ObjectSubtype)
        {
            p.life = ObjectMistLife;
            p.scale = F32(static_cast<double>(random.Next() % MistSpan + MistSpan) * Tenth + scaleArgument);
        }
        else
            p.scale = F32(static_cast<double>(random.Next() % MistSpan + MistSpan) * Tenth);
        const int fall = -3 - random.Next() % FallSpan;
        // 0x543fcd queries Scene after R2 and before the vz store at 0x543fd8. The /1 overrides that light (0x544057)
        // without reading it: it does not ask for it.
        const bool arm = p.subtype == ArmMistSubtype;
        if (!scene && !arm)
            return Core::Unexpected(ParticleError::MissingScene);
        p.velocity.z = static_cast<float>(fall);
        if (scene)
            p.light = glm::vec3(*scene == SelectionScene ? 0.25f : 0.4f);
        Jitter(p, random, ShortLife, 40);
        if (arm)
        {
            // 0x544057..0x544085: the light (0; 0.4f; 0.4f) from ebx, life 50 and another fall −3 − rand() % 3: seven
            // rand().
            constexpr float Cyan = std::bit_cast<float>(0x3ecccccdu);
            constexpr std::int32_t ArmLife = 50;
            p.light = {0.0f, Cyan, Cyan};
            p.life = ArmLife;
            p.velocity.z = static_cast<float>(-3 - random.Next() % FallSpan);
        }
        if (p.subtype == BootsMistSubtype)
            CreateBootsMist(p, random, lightArgument);
        return {};
    }
    constexpr int FallSpan = 5, FallBase = 7;
    constexpr float FallScale = 1.6f;
    if (p.type == KalimaWaterfallLast && p.subtype == BrazierFallSubtype)
    {
        // 0x543f21..0x543f47: the argument's scale plus 0.6f (0x6b8a20) and the fall −12 − rand() % 5.
        constexpr int BrazierFallBase = 12;
        constexpr double BrazierGrowth = static_cast<double>(std::bit_cast<float>(0x3f19999au));
        p.scale = F32(static_cast<double>(scaleArgument) + BrazierGrowth);
        p.velocity.z = static_cast<float>(-BrazierFallBase - random.Next() % FallSpan);
    }
    else
    {
        p.scale = p.subtype == ObjectSubtype ? F32(static_cast<double>(scaleArgument) + FallScale) : FallScale;
        p.velocity.z = static_cast<float>(-FallBase - random.Next() % FallSpan);
    }
    p.light = glm::vec3(0.2f);
    return {};
}
// 0x544295..0x544318 (6f2/2, 6f3/2) and 0x543e14..0x543e97 (6f4/4), the splashes of Kundun's joint: life 6, the rise
// (+0x50) ((rand() % 100) · 0.01f · 4 + x angle · 1.2f, added on the x87 in that order), the scale (rand() % 6) ·
// step + the requested one, the turn and the start at the position. Three rand().
void CreateKundunSplash(Particle& p, Crt::CrtRandom& random, double scaleStep)
{
    constexpr std::int32_t SplashLife = 6;
    constexpr int RiseSpan = 100, ScaleSpan = 6;
    constexpr double RiseFactor = 4.0;                                                    // 0x6b8a44
    constexpr double TiltFactor = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1.2f
    const float scaleArgument = p.scale;
    p.life = SplashLife;
    const double rise = static_cast<double>(random.Next() % RiseSpan) * Hundredth * RiseFactor;
    p.verticalSpeed = F32(static_cast<double>(p.angles.x) * TiltFactor + rise);
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * scaleStep + scaleArgument);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.start = p.position;
}
// 0x5480fd..0x548158 (6f2/2, 6f3/2) and 0x547f6b..0x547fa2 (6f4/4): x += cos(z angle) · 20 and y += sin(z angle) · 20
// with fcos/fsin of the raw angle (not converted to radians), z += the rise and the turn + 1. The 6f4/4 goes on with
// the 6f4 fade (0x547fc1); the others multiply the light by 0.9090909f (0x6ba26c). No rand().
Core::Expected<void, ParticleError> StepKundunSplash(Particle& p, IParticleTrigonometry* trigonometry)
{
    constexpr float Reach = 20.0f; // 0x6ba2f8 (double)
    if (trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    const auto cosine = trigonometry->CosineProductPC53(static_cast<double>(p.angles.z), Reach);
    if (!cosine.HasValue())
        return Core::Unexpected(cosine.Error());
    p.position.x = F32(cosine.Value() + static_cast<double>(p.position.x));
    const auto sine = trigonometry->SineProductPC53(static_cast<double>(p.angles.z), Reach);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    p.position.y = F32(sine.Value() + static_cast<double>(p.position.y));
    p.position.z = F32(static_cast<double>(p.verticalSpeed) + static_cast<double>(p.position.z));
    p.rotation = F32(static_cast<double>(p.rotation) + 1.0);
    if (p.type != KalimaWaterfallLast)
    {
        FadeLight(p, MistFade);
        return {};
    }
    constexpr int LastFade = 8;
    if (p.life < LastFade)
        FadeLight(p, Fade);
    else if (p.life > ShortLife)
        FadeLight(p, Brighten);
    return {};
}
void StepFlareSix(Particle& p, Crt::CrtRandom& random)
{
    constexpr int WalkSpan = 2001, WalkCenter = 1000;
    const float x = F32(static_cast<double>(random.Next() % WalkSpan - WalkCenter) * WalkFactor);
    const double y = static_cast<double>(random.Next() % WalkSpan - WalkCenter) * WalkFactor;
    const float halfX = F32(static_cast<double>(x) * 0.5);
    p.position.x = F32(static_cast<double>(p.position.x) + halfX);
    p.position.y = F32(static_cast<double>(p.position.y) + y * 0.5);
    p.position.z = F32(static_cast<double>(p.position.z) + Rise);
    const double scale = static_cast<double>(p.scale) * Damping;
    p.scale = F32(scale);
    FadeLight(p, Damping);
    if (!(scale > Tenth))
    {
        p.life = -1;
        p.alive = false;
    }
}
// 0x544c19/0x544c44..0x544dd6: the 5ac/0 with k = 0.5f. The x is stored in a float four times and the y once; the z
// adds k · 5 and 0x7609bc0 times k, which nothing writes (zero). The scale drops 0.05f, the light copies it and it
// dies with the stored scale at 0.1f or less (or NaN).
void StepFlareZero(Particle& p, Crt::CrtRandom& random, const glm::vec2& wind)
{
    constexpr int WalkSpan = 2001, WalkCenter = 1000;
    constexpr double Half = Math::HalfAsDouble, Lift = 5.0, Unwritten = 0.0;
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
    float x = F32(static_cast<double>(random.Next() % WalkSpan - WalkCenter) * WalkFactor);
    double y = static_cast<double>(random.Next() % WalkSpan - WalkCenter) * WalkFactor;
    x = F32(static_cast<double>(x) + static_cast<double>(wind.x));
    y += static_cast<double>(wind.y);
    x = F32(static_cast<double>(x) * Half);
    y *= Half;
    const float z = F32(Unwritten * Half);
    p.position.x = F32(static_cast<double>(x) + static_cast<double>(p.position.x));
    p.position.y = F32(y + static_cast<double>(p.position.y));
    p.position.z = F32((Half * Lift + static_cast<double>(p.position.z)) + static_cast<double>(z));
    p.scale = F32(static_cast<double>(p.scale) - Shrink);
    p.light = glm::vec3(p.scale);
    if (!(p.scale > 0.1f))
    {
        p.life = -1;
        p.alive = false;
    }
}
// 0x544b8e..0x544c04: the 5ac/9 (the trail of effect 0xb3 of Aida's 0x46) shrinks the scale 0.011f; the comparison
// with 0 reads the x87 subtraction unrounded to float (fst). With the live byte already off by the common life, or
// with the scale not above 0 (or NaN), the life goes to 0 without turning off the byte, which the next step's
// subtraction turns off. From 75 life the light rises by 1.05f; below, it drops by 0.952381f.
void StepFlareNine(Particle& p)
{
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3c343958u));
    constexpr double Grow = static_cast<double>(std::bit_cast<float>(0x3f866666u));
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));
    constexpr std::int32_t BrightLife = 0x4b;
    const double scale = static_cast<double>(p.scale) - Shrink;
    p.scale = F32(scale);
    if (!p.alive)
        p.life = 0;
    if (!(scale > 0.0))
        p.life = 0;
    FadeLight(p, p.life >= BrightLife ? Grow : Dim);
}
Core::Expected<void, ParticleError> StepFlareSeven(Particle& p, const float* clock, IParticleTrigonometry* trigonometry)
{
    if (clock == nullptr || trigonometry == nullptr)
        return Core::Unexpected(ParticleError::MissingClock);
    constexpr float Oscillation = 5;
    const auto sine = trigonometry->SineProductPC53(static_cast<double>(p.life) + *clock, Oscillation);
    if (!sine.HasValue())
        return Core::Unexpected(sine.Error());
    p.position.z = F32((sine.Value() + LifeSpan) + p.position.z);
    p.rotation = F32(static_cast<double>(p.rotation) + LongLife);
    if (p.life < LifeSpan)
        FadeLight(p, Fade);
    if (!(p.scale > Tenth))
    {
        p.life = -1;
        p.alive = false;
    }
    return {};
}
// 0x547a40..0x547cb4, the fanta with a non-null owner (+0x40): the /1 and the /2 lose two life with their action
// (+0x111) at 1 (0x547a4d); the /3 and the /4 look for Monster82's left or right hand (0x547c1e, 0x4010b0) and, if it
// is there, the distance d (the subtractions, squares and root on the x87, ((dz² + dy²) + dx²)) shrinks the scale
// d · 0.003f (0x6b89cc) and dims the light d · 0.08f (0x6ba1ac), which the common step overrides later with the life.
Core::Expected<void, ParticleError> StepOwnedFanta(Particle& p, const ParticleOwnerStates& ownerStates)
{
    if (p.subtype < 1 || p.subtype > LastOwnedFantaSubtype || p.typedOwner.kind == ParticleOwnerKind::None)
        return {};
    const auto owner = ownerStates ? ownerStates(p.typedOwner) : std::nullopt;
    if (!owner)
        return Core::Unexpected(ParticleError::MissingOwner);
    constexpr std::int32_t ShakenSubtype = 2, ShakingAction = 1, ShakenLife = 2;
    if (p.subtype <= ShakenSubtype)
    {
        if (owner->action == ShakingAction)
            p.life -= ShakenLife;
        return {};
    }
    constexpr std::int32_t LeftHandSubtype = 3;
    const std::string_view hand = p.subtype == LeftHandSubtype ? "Monster82_LHand" : "Monster82_RHand";
    const auto bone = owner->namedBone ? owner->namedBone(hand) : std::nullopt;
    if (!bone)
        return {};
    constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3b449ba6u));
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3da3d70au));
    const double dx = static_cast<double>(p.position.x) - static_cast<double>(bone->x);
    const double dy = static_cast<double>(p.position.y) - static_cast<double>(bone->y);
    const double dz = static_cast<double>(p.position.z) - static_cast<double>(bone->z);
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    p.scale = F32(static_cast<double>(p.scale) - distance * Shrink);
    const double dim = distance * Dim;
    for (int axis = 0; axis < 3; ++axis)
        p.light[axis] = F32(static_cast<double>(p.light[axis]) - dim);
    return {};
}
// 0x547cba..0x547d3d: the scale drops 0.02f (the /7, 0.1f and it also rises 2), not below 0; the x and y speed by
// 0.95f, the z rises 1 and the light is life · 0.04f.
void StepFanta(Particle& p)
{
    constexpr double SwirlLift = 2.0; // 0x6b8818
    if (p.subtype == SwirlFantaSubtype)
    {
        p.scale = F32(static_cast<double>(p.scale) - Tenth);
        p.position.z = F32(static_cast<double>(p.position.z) + SwirlLift);
    }
    else
        p.scale = F32(static_cast<double>(p.scale) - FantaShrink);
    if (!(p.scale >= 0.0f))
        p.scale = 0;
    p.velocity.x = F32(static_cast<double>(p.velocity.x) * Damping);
    p.velocity.y = F32(static_cast<double>(p.velocity.y) * Damping);
    p.position.z = F32(static_cast<double>(p.position.z) + 1);
    p.light = glm::vec3(F32(static_cast<double>(p.life) * FantaLight));
}
void StepWaterfall(Particle& p, Crt::CrtRandom& random)
{
    if (p.type == KalimaWaterfallMist)
    {
        p.scale = F32(static_cast<double>(p.scale) + MistGrowth);
        for (int axis = 0; axis < 2; ++axis)
            p.velocity[axis] = F32(static_cast<double>(random.Next() % ShortLife - LifeSpan) * Tenth);
        // 0x548059: fst, the sum stays on the x87 stack for the 6f1/3 subtraction.
        const double rise = static_cast<double>(p.velocity.z) + Tenth;
        p.velocity.z = F32(rise);
        if (p.life < LifeSpan)
            FadeLight(p, MistFade);
        // 0x54808a..0x5480ab: the /1 also fades by 0.95238101f (0x6ba264).
        if (p.subtype == ArmMistSubtype)
            FadeLight(p, static_cast<double>(std::bit_cast<float>(0x3f73cf3eu)));
        if (p.subtype == BootsMistSubtype)
        {
            // 0x5480b7..0x5480ed: the scale drops 0.038f, vz is the unrounded sum minus 0.02f and the light by
            // 0.98039216f.
            constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d1ba5e3u));    // 0x6ba344, 0.038f
            constexpr double Sink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));      // 0x6b8b90, 0.02f
            constexpr double BootsFade = static_cast<double>(std::bit_cast<float>(0x3f7afafbu)); // 0x6ba340
            p.scale = F32(static_cast<double>(p.scale) - Shrink);
            p.velocity.z = F32(rise - Sink);
            FadeLight(p, BootsFade);
        }
        return;
    }
    if (p.type == WaterfallSplash || p.type == WaterfallSplash + 1)
    {
        p.scale = F32(static_cast<double>(p.scale) + SmallGrowth);
        p.velocity.z = F32(static_cast<double>(p.velocity.z) - Rise);
        FadeLight(p, MistFade);
        return;
    }
    if (p.type == KalimaWaterfallLast && p.subtype == 1)
    {
        constexpr int WalkSpan = 10, WalkCenter = 5;
        p.scale = F32(static_cast<double>(p.scale) + Tenth);
        for (int axis = 0; axis < 2; ++axis)
            p.position[axis] = F32(static_cast<double>(random.Next() % WalkSpan - WalkCenter) + p.position[axis]);
        constexpr int FootstepFade = 8;
        if (p.life < FootstepFade)
            FadeLight(p, Fade);
        else if (p.life > ShortLife)
            FadeLight(p, Brighten);
        return;
    }
    if (p.type == KalimaWaterfallLast && p.subtype == KundunDebrisSubtype)
    {
        // 0x547ee2..0x547fbe: with the scale below 1 (FCOMP with C0: less or unordered), 0.1f more; x and y plus
        // rand() % 10 − 5 (fild, fsub of 5 and fadd) and the rise 1 less. Then the 6f4 fade (0x547fc1).
        constexpr int WalkSpan = 10;
        constexpr double WalkCenter = 5.0, Sink = 1.0;
        if (!(static_cast<double>(p.scale) >= 1.0))
            p.scale = F32(static_cast<double>(p.scale) + Tenth);
        for (int axis = 0; axis < 2; ++axis)
            p.position[axis] = F32((static_cast<double>(random.Next() % WalkSpan) - WalkCenter) +
                                   static_cast<double>(p.position[axis]));
        p.velocity.z = F32(static_cast<double>(p.velocity.z) - Sink);
    }
    // 0x547fa4: the 6f4/6 jumps to 0x547fc1, without the scale or the fall.
    else if (p.type != KalimaWaterfallLast || p.subtype != BrazierFallSubtype)
    {
        p.scale = F32(static_cast<double>(p.scale) - SmallGrowth);
        // 0x547f58: the 6f4/3 also turns 4 between the scale and the fall.
        if (p.type == KalimaWaterfallLast && p.subtype == KundunFallSubtype)
            p.rotation = F32(static_cast<double>(p.rotation) + KundunFallTurn);
        p.velocity.z = F32(static_cast<double>(p.velocity.z) + Tenth);
    }
    constexpr int FirstFade = 5, LastFade = 8;
    if (p.life < (p.type == KalimaWaterfallFirst ? FirstFade : LastFade))
        FadeLight(p, Fade);
    else if (p.life > ShortLife)
        FadeLight(p, Brighten);
}
} // namespace
bool IsKalimaParticle(std::int32_t type, std::int32_t subtype)
{
    const bool objectWaterfall =
        ((type == KalimaWaterfallFirst || type == KalimaWaterfallMist) && subtype == ObjectSubtype) ||
        ((type == WaterfallSplash || type == WaterfallSplash + 1) && subtype == SplashObjectSubtype);
    // 0x543b82 and 0x547a40: all the subtypes; the /1..4 read their owner in the step (0x547a4d and 0x547a73).
    const bool fanta = type == KalimaFanta || type == CrywolfFanta;
    return objectWaterfall || fanta ||
           (type == KalimaWaterfallLast &&
            (subtype == 1 || subtype == KundunDebrisSubtype || subtype == KundunFallSubtype ||
             subtype == KundunSplashLastSubtype || subtype == BrazierFallSubtype)) ||
           ((type == WaterfallSplash || type == WaterfallSplash + 1) && subtype == KundunSplashSubtype) ||
           (type == KalimaWaterfallMist && (subtype == ArmMistSubtype || subtype == BootsMistSubtype)) ||
           (type == KalimaFlare &&
            (subtype == FlareZero || subtype == FlareTrail || subtype == FlareFive || subtype == FlareSix ||
             subtype == FlareSeven || subtype == FlareEight || subtype == FlareNine)) ||
           (subtype == 0 && type >= KalimaWaterfallFirst && type <= KalimaWaterfallLast);
}
Core::Expected<void, ParticleError> CreateKalimaParticle(Particle& p, Crt::CrtRandom& random,
                                                         std::optional<std::int32_t> scene)
{
    if (p.type == KalimaFlare)
    {
        // 0x54007f: the 5ac/1 (the trail of Icarus's 0x5ac effect) only lives 50, without rand(). 0x540140: the /5
        // lives 25, without rand(). 0x53ffb9: the 5ac/9 lives ftol(scale · 40), without rand().
        constexpr std::int32_t TrailLife = 50, FiveLife = 25;
        constexpr double NineLifePerScale = 40.0;
        if (p.subtype == FlareTrail)
            p.life = TrailLife;
        else if (p.subtype == FlareFive)
            p.life = FiveLife;
        else if (p.subtype == FlareNine)
            p.life = Math::FtolLow(static_cast<double>(p.scale) * NineLifePerScale);
        else
            CreateFlare(p, random);
        return {};
    }
    if (p.type == KalimaFanta || p.type == CrywolfFanta)
    {
        // 0x543b82..0x543be5: life 24, the two speed rand() and, with the /5, the life at 20 (the /6, 32). 0x543aed:
        // the /7 lives 15, with the scale rand() % 30 · 0.01f plus the requested one and the position shifted (rand() %
        // 10) · 0.1f − 0.5 in x, y and z, before the speed.
        constexpr int FantaLife = 24, ShortFantaLife = 20, LongFantaLife = 32, Center = 5, SwirlLife = 15;
        constexpr int SwirlSpan = 30, ShiftSpan = 10;
        if (p.subtype == SwirlFantaSubtype)
        {
            p.life = SwirlLife;
            p.scale = F32(static_cast<double>(random.Next() % SwirlSpan) * Hundredth + static_cast<double>(p.scale));
            for (int axis = 0; axis < 3; ++axis)
                p.position[axis] = F32((static_cast<double>(random.Next() % ShiftSpan) * Tenth - 0.5) +
                                       static_cast<double>(p.position[axis]));
        }
        else
            p.life = FantaLife;
        p.velocity.x = F32(static_cast<double>(random.Next() % LifeSpan - Center) * static_cast<double>(0.4f));
        p.velocity.y = 0;
        p.velocity.z = F32(static_cast<double>(random.Next() % LifeSpan + Center) * static_cast<double>(0.2f));
        if (p.subtype == ShortFantaSubtype)
            p.life = ShortFantaLife;
        else if (p.subtype == LongFantaSubtype)
            p.life = LongFantaLife; // 0x543bdc..0x543be5
        p.start = p.position;
        return {};
    }
    if (p.type == KalimaWaterfallLast && p.subtype == KundunDebrisSubtype)
    {
        // 0x543d68..0x543dba: life 20, the turn (the 0.5 scale is written between the rand() and its remainder), 50
        // higher (0x6b86b0, fld, fadd and fstp) and the rise rand() % 5 + 10 (fild). It does not touch the light: the
        // requested one stays.
        constexpr int RiseSpan = 5, RiseBase = 10;
        constexpr double Lift = 50.0;
        p.life = ShortLife;
        p.scale = 0.5f;
        p.rotation = static_cast<float>(random.Next() % FullTurn);
        p.position.z = F32(static_cast<double>(p.position.z) + Lift);
        p.velocity.z = static_cast<float>(random.Next() % RiseSpan + RiseBase);
        return {};
    }
    if (p.type == KalimaWaterfallLast && p.subtype == KundunFallSubtype)
    {
        // 0x543dc0..0x543e0a: life 20, the turn, the scale 1.6f (0x3fcccccd) and the fall −7 − rand() % 5. Unlike the
        // 6f4/0, it does not touch the light: the requested one stays.
        constexpr int FallSpan = 5, FallBase = 7;
        p.life = ShortLife;
        p.rotation = static_cast<float>(random.Next() % FullTurn);
        p.scale = std::bit_cast<float>(0x3fcccccdu);
        p.velocity.z = static_cast<float>(-FallBase - random.Next() % FallSpan);
        return {};
    }
    if (p.type == KalimaWaterfallLast && p.subtype == KundunSplashLastSubtype)
    {
        // 0x543e14..0x543e97: like the 6f2/2, with a scale of 0.2f (0x6b8854) per step.
        CreateKundunSplash(p, random, static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)));
        return {};
    }
    if (p.subtype == KundunSplashSubtype && (p.type == WaterfallSplash || p.type == WaterfallSplash + 1))
    {
        // 0x544295..0x544318: a scale of 0.1f (0x6b87b8) per step.
        CreateKundunSplash(p, random, Tenth);
        return {};
    }
    if (p.type < WaterfallSplash || p.type == KalimaWaterfallLast)
        return CreateWaterfall(p, random, scene);
    constexpr int RiseSpan = 5, RiseBase = 2;
    if (p.subtype == SplashObjectSubtype)
    {
        const float scaleArgument = p.scale;
        p.rotation = static_cast<float>(random.Next() % FullTurn);
        p.scale = F32(static_cast<double>(random.Next() % LifeSpan + LifeSpan) * FantaShrink);
        p.life = ShortLife;
        p.velocity.z = static_cast<float>(random.Next() % RiseSpan + RiseBase);
        p.scale = F32(static_cast<double>(random.Next() % LifeSpan + LifeSpan) * FantaShrink + scaleArgument);
        Jitter(p, random, 40, ShortLife);
        return {};
    }
    p.life = ShortLife;
    p.velocity.z = static_cast<float>(random.Next() % RiseSpan + RiseBase);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.scale = F32(static_cast<double>(random.Next() % LifeSpan + LifeSpan) * FantaShrink);
    Jitter(p, random, 40, ShortLife);
    return {};
}
Core::Expected<void, ParticleError> StepKalimaParticle(Particle& p, Crt::CrtRandom& random, const float* clock,
                                                       IParticleTrigonometry* trigonometry, const glm::vec2& wind,
                                                       const ParticleOwnerStates& ownerStates)
{
    if (p.type == KalimaFlare)
    {
        if (p.subtype == FlareZero || p.subtype == FlareEight)
        {
            // 0x5448e1..0x544903 and 0x544c09..0x544c19: the /8 enters with the cmp by 8 still equal and takes the k =
            // 0.5f of the /0; after its death, 0x544dd9..0x544dfd sets its light (scale; scale · 0.5; scale · 0.3f).
            StepFlareZero(p, random, wind);
            if (p.subtype == FlareEight)
            {
                constexpr double ThreeTenths = static_cast<double>(std::bit_cast<float>(0x3e99999au));
                p.light = {p.scale, F32(static_cast<double>(p.scale) * 0.5),
                           F32(static_cast<double>(p.scale) * ThreeTenths)};
            }
        }
        else if (p.subtype == FlareTrail || p.subtype == FlareFive)
        {
            // 0x5449e1..0x544a11 (the /5 through 0x544a19): the light by 0.9f and the scale by 0.95f.
            constexpr double TrailFade = static_cast<double>(std::bit_cast<float>(0x3f666666u));
            FadeLight(p, TrailFade);
            p.scale = F32(static_cast<double>(p.scale) * Damping);
        }
        else if (p.subtype == FlareSix)
            StepFlareSix(p, random);
        else if (p.subtype == FlareNine)
            StepFlareNine(p);
        else
            return StepFlareSeven(p, clock, trigonometry);
    }
    else if (p.type == KalimaFanta || p.type == CrywolfFanta)
    {
        if (auto owned = StepOwnedFanta(p, ownerStates); !owned.HasValue())
            return owned;
        StepFanta(p);
    }
    else if ((p.type == KalimaWaterfallLast && p.subtype == KundunSplashLastSubtype) ||
             (p.subtype == KundunSplashSubtype && (p.type == WaterfallSplash || p.type == WaterfallSplash + 1)))
        return StepKundunSplash(p, trigonometry);
    else
        StepWaterfall(p, random);
    return {};
}
} // namespace Client::Particles
