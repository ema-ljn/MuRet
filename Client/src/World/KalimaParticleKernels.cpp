#include "World/KalimaParticleKernels.h"
#include "Core/Expected.h"

#include "World/IParticleTrigonometry.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::World
{
namespace
{
// Constantes binary32 del ASM543b82..544260 y544912..5481a9.
constexpr double Tenth = static_cast<double>(0.1f), Hundredth = static_cast<double>(0.01f);
constexpr double SmallGrowth = static_cast<double>(0.005f), MistGrowth = static_cast<double>(0.03f);
constexpr double FantaShrink = static_cast<double>(0.02f), FantaLight = static_cast<double>(0.04f);
constexpr double Damping = static_cast<double>(0.95f), Rise = 2.5;
constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
constexpr double MistFade = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));
constexpr double Brighten = static_cast<double>(1.1f);
// 6ba3e8: bytes18 b7 d1 39; conservar el patrónbinary32 del drift54492b/544950.
constexpr double WalkFactor = static_cast<double>(std::bit_cast<float>(0x39d1b718u));
constexpr int FullTurn = 360, LifeSpan = 10, LongLife = 30, ShortLife = 20;
constexpr int FlareSix = 6, FlareSeven = 7, SelectionScene = 4;
constexpr std::int32_t WaterfallSplash = 0x6f2;
float F32(double value)
{
    return static_cast<float>(value);
}
void FadeLight(Particle& p, double factor)
{
    for (int axis = 0; axis < 3; ++axis)
        p.light[axis] = F32(static_cast<double>(p.light[axis]) * factor);
}
void Jitter(Particle& p, Render::Models::CrtRandom& random, int xySpan, int zSpan)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        const int span = axis == 2 ? zSpan : xySpan;
        const int offset = random.Next() % span - span / 2;
        p.position[axis] = F32(static_cast<double>(offset) + p.position[axis]);
    }
}
void CreateFlare(Particle& p, Render::Models::CrtRandom& random)
{
    const bool six = p.subtype == FlareSix;
    p.life = random.Next() % LifeSpan + (six ? LifeSpan : LongLife);
    p.verticalSpeed = static_cast<float>(random.Next() % LifeSpan + LifeSpan);
    const int size = random.Next() % (six ? 50 : 15) + (six ? 50 : LongLife);
    p.scale = F32((static_cast<double>(size) * p.scale) * Hundredth);
}
Core::Expected<void, ParticleError> CreateWaterfall(Particle& p, Render::Models::CrtRandom& random,
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
    p.life = LongLife;
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    if (p.type == KalimaWaterfallMist)
    {
        constexpr int MistSpan = 6, FallSpan = 3;
        p.scale = F32(static_cast<double>(random.Next() % MistSpan + MistSpan) * Tenth);
        const int fall = -3 - random.Next() % FallSpan;
        // 543fcd consultaScene después de R2 y antes del store de vz543fd8.
        if (!scene)
            return Core::Unexpected(ParticleError::MissingScene);
        p.velocity.z = static_cast<float>(fall);
        p.light = glm::vec3(*scene == SelectionScene ? 0.25f : 0.4f);
        Jitter(p, random, ShortLife, 40);
        return {};
    }
    constexpr int FallSpan = 5, FallBase = 7;
    p.scale = 1.6f;
    p.velocity.z = static_cast<float>(-FallBase - random.Next() % FallSpan);
    p.light = glm::vec3(0.2f);
    return {};
}
void StepFlareSix(Particle& p, Render::Models::CrtRandom& random)
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
void StepFanta(Particle& p)
{
    p.scale = F32(static_cast<double>(p.scale) - FantaShrink);
    if (!(p.scale >= 0.0f))
        p.scale = 0;
    p.velocity.x = F32(static_cast<double>(p.velocity.x) * Damping);
    p.velocity.y = F32(static_cast<double>(p.velocity.y) * Damping);
    p.position.z = F32(static_cast<double>(p.position.z) + 1);
    p.light = glm::vec3(F32(static_cast<double>(p.life) * FantaLight));
}
void StepWaterfall(Particle& p, Render::Models::CrtRandom& random)
{
    if (p.type == KalimaWaterfallMist)
    {
        p.scale = F32(static_cast<double>(p.scale) + MistGrowth);
        for (int axis = 0; axis < 2; ++axis)
            p.velocity[axis] = F32(static_cast<double>(random.Next() % ShortLife - LifeSpan) * Tenth);
        p.velocity.z = F32(static_cast<double>(p.velocity.z) + Tenth);
        if (p.life < LifeSpan)
            FadeLight(p, MistFade);
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
    p.scale = F32(static_cast<double>(p.scale) - SmallGrowth);
    p.velocity.z = F32(static_cast<double>(p.velocity.z) + Tenth);
    constexpr int FirstFade = 5, LastFade = 8;
    if (p.life < (p.type == KalimaWaterfallFirst ? FirstFade : LastFade))
        FadeLight(p, Fade);
    else if (p.life > ShortLife)
        FadeLight(p, Brighten);
}
} // namespace
bool IsKalimaParticle(std::int32_t type, std::int32_t subtype)
{
    return (type == KalimaWaterfallLast && subtype == 1) ||
           (type == KalimaFlare && (subtype == FlareSix || subtype == FlareSeven)) ||
           (subtype == 0 && (type == KalimaFanta || (type >= KalimaWaterfallFirst && type <= KalimaWaterfallLast)));
}
Core::Expected<void, ParticleError> CreateKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                         std::optional<std::int32_t> scene)
{
    if (p.type == KalimaFlare)
    {
        CreateFlare(p, random);
        return {};
    }
    if (p.type == KalimaFanta)
    {
        constexpr int FantaLife = 24, Center = 5;
        p.life = FantaLife;
        p.velocity.x = F32(static_cast<double>(random.Next() % LifeSpan - Center) * static_cast<double>(0.4f));
        p.velocity.y = 0;
        p.velocity.z = F32(static_cast<double>(random.Next() % LifeSpan + Center) * static_cast<double>(0.2f));
        p.start = p.position;
        return {};
    }
    if (p.type < WaterfallSplash || p.type == KalimaWaterfallLast)
        return CreateWaterfall(p, random, scene);
    constexpr int RiseSpan = 5, RiseBase = 2;
    p.life = ShortLife;
    p.velocity.z = static_cast<float>(random.Next() % RiseSpan + RiseBase);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.scale = F32(static_cast<double>(random.Next() % LifeSpan + LifeSpan) * FantaShrink);
    Jitter(p, random, 40, ShortLife);
    return {};
}
Core::Expected<void, ParticleError> StepKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                       const float* clock, IParticleTrigonometry* trigonometry)
{
    if (p.type == KalimaFlare)
    {
        if (p.subtype == FlareSix)
            StepFlareSix(p, random);
        else
            return StepFlareSeven(p, clock, trigonometry);
    }
    else if (p.type == KalimaFanta)
        StepFanta(p);
    else
        StepWaterfall(p, random);
    return {};
}
} // namespace Client::World
