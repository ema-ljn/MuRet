#include "World/KalimaParticleKernels.h"
#include "Core/Expected.h"

#include "World/Ftol.h"
#include "World/IParticleTrigonometry.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
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
constexpr int FlareZero = 0, FlareTrail = 1, FlareSix = 6, FlareSeven = 7, FlareNine = 9, SelectionScene = 4;
constexpr std::int32_t WaterfallSplash = WaterfallSplashFirst;
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
// 0x540151..0x54018f: el 5ac/0 (y el /2 y el /8) son las mismas instrucciones que el /6.
void CreateFlare(Particle& p, Render::Models::CrtRandom& random)
{
    const bool six = p.subtype == FlareSix || p.subtype == FlareZero;
    p.life = random.Next() % LifeSpan + (six ? LifeSpan : LongLife);
    p.verticalSpeed = static_cast<float>(random.Next() % LifeSpan + LifeSpan);
    const int size = random.Next() % (six ? 50 : 15) + (six ? 50 : LongLife);
    p.scale = F32((static_cast<double>(size) * p.scale) * Hundredth);
}
// 543f04..543fae y 54417f..544149: 6f0/2 y 6f1/2 suman la escala del argumento (la del prefijo, todavía
// en +0x10) y 6f1/2 vive 40; 6f2,6f3/4 sortea giro y escala antes de su vida, velocidad y segunda escala.
constexpr std::int32_t ObjectSubtype = 2, SplashObjectSubtype = 4;
// 0x544088..0x544153 y 0x5480ae..0x5480f0: la 6f1/3 de las botas del conjunto completo (0x50ff37).
constexpr std::int32_t BootsMistSubtype = 3;
// 0x544095..0x54414c: después de lo de la neblina, la textura cra_04 (0x5ae), la luz del argumento (pisa la de la
// escena), vida 12, otro giro, vz 2, la escala (rand() % 3 + 3) · 0,18f y otro corrimiento de ±10, ±10 y ±20. Cinco
// rand() más: once en total.
void CreateBootsMist(Particle& p, Render::Models::CrtRandom& random, const glm::vec3& lightArgument)
{
    constexpr std::int32_t BootsTexture = 0x5ae;
    constexpr std::int32_t BootsLife = 12;
    constexpr float BootsRise = 2.0f;
    constexpr int ScaleSpan = 3;
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3e3851ecu)); // 0x6ba2d0, 0,18f
    p.texture = BootsTexture;
    p.light = lightArgument;
    p.life = BootsLife;
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.velocity.z = BootsRise;
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan + ScaleSpan) * ScaleStep);
    Jitter(p, random, ShortLife, 40);
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
        // 543fcd consultaScene después de R2 y antes del store de vz543fd8.
        if (!scene)
            return Core::Unexpected(ParticleError::MissingScene);
        p.velocity.z = static_cast<float>(fall);
        p.light = glm::vec3(*scene == SelectionScene ? 0.25f : 0.4f);
        Jitter(p, random, ShortLife, 40);
        if (p.subtype == BootsMistSubtype)
            CreateBootsMist(p, random, lightArgument);
        return {};
    }
    constexpr int FallSpan = 5, FallBase = 7;
    constexpr float FallScale = 1.6f;
    if (p.type == KalimaWaterfallLast && p.subtype == BrazierFallSubtype)
    {
        // 0x543f21..0x543f47: la escala del argumento más 0,6f (0x6b8a20) y la caída −12 − rand() % 5.
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
// 0x544c19/0x544c44..0x544dd6: el 5ac/0 con k = 0,5f. La x se guarda en float cuatro veces y la y una;
// la z suma k · 5 y el 0x7609bc0 por k, que nada escribe (cero). La escala baja 0,05f, la luz la copia y
// muere con la escala guardada en 0,1f o menos (o NaN).
void StepFlareZero(Particle& p, Render::Models::CrtRandom& random, const glm::vec2& wind)
{
    constexpr int WalkSpan = 2001, WalkCenter = 1000;
    constexpr double Half = 0.5, Lift = 5.0, Unwritten = 0.0;
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
// 0x544b8e..0x544c04: el 5ac/9 (la estela del efecto 0xb3 del 0x46 de Aida) achica la escala 0,011f; la
// comparación con 0 lee la resta de la x87 sin redondear a float (fst). Con el byte vivo ya apagado por la
// vida común, o con la escala que no queda por encima de 0 (o NaN), la vida va a 0 sin apagar el byte, que
// apaga la resta del paso siguiente. Desde 75 de vida la luz sube por 1,05f; debajo, baja por 0,952381f.
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
        // 0x548059: fst, la suma queda en la pila para la resta de la 6f1/3.
        const double rise = static_cast<double>(p.velocity.z) + Tenth;
        p.velocity.z = F32(rise);
        if (p.life < LifeSpan)
            FadeLight(p, MistFade);
        if (p.subtype == BootsMistSubtype)
        {
            // 0x5480b7..0x5480ed: la escala baja 0,038f, vz es la suma sin redondear menos 0,02f y la luz por
            // 0,98039216f.
            constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3d1ba5e3u));    // 0x6ba344, 0,038f
            constexpr double Sink = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));      // 0x6b8b90, 0,02f
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
    // 0x547fa4: la 6f4/6 salta a 0x547fc1, sin la escala ni la caída.
    if (p.type != KalimaWaterfallLast || p.subtype != BrazierFallSubtype)
    {
        p.scale = F32(static_cast<double>(p.scale) - SmallGrowth);
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
    const bool fanta = (type == KalimaFanta || type == CrywolfFanta) && (subtype == 0 || subtype == ShortFantaSubtype);
    return objectWaterfall || fanta ||
           (type == KalimaWaterfallLast && (subtype == 1 || subtype == BrazierFallSubtype)) ||
           (type == KalimaWaterfallMist && subtype == BootsMistSubtype) ||
           (type == KalimaFlare && (subtype == FlareZero || subtype == FlareTrail || subtype == FlareSix ||
                                    subtype == FlareSeven || subtype == FlareNine)) ||
           (subtype == 0 && type >= KalimaWaterfallFirst && type <= KalimaWaterfallLast);
}
Core::Expected<void, ParticleError> CreateKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                         std::optional<std::int32_t> scene)
{
    if (p.type == KalimaFlare)
    {
        // 0x54007f: el 5ac/1 (la estela del efecto 0x5ac de Icarus) sólo vive 50, sin rand(). 0x53ffb9: el
        // 5ac/9 vive ftol(escala · 40), sin rand().
        constexpr std::int32_t TrailLife = 50;
        constexpr double NineLifePerScale = 40.0;
        if (p.subtype == FlareTrail)
            p.life = TrailLife;
        else if (p.subtype == FlareNine)
            p.life = FtolLow(static_cast<double>(p.scale) * NineLifePerScale);
        else
            CreateFlare(p, random);
        return {};
    }
    if (p.type == KalimaFanta || p.type == CrywolfFanta)
    {
        // 0x543b82..0x543bd5: vida 24, los dos rand() de la velocidad y, con la /5, la vida en 20.
        constexpr int FantaLife = 24, ShortFantaLife = 20, Center = 5;
        p.life = FantaLife;
        p.velocity.x = F32(static_cast<double>(random.Next() % LifeSpan - Center) * static_cast<double>(0.4f));
        p.velocity.y = 0;
        p.velocity.z = F32(static_cast<double>(random.Next() % LifeSpan + Center) * static_cast<double>(0.2f));
        if (p.subtype == ShortFantaSubtype)
            p.life = ShortFantaLife;
        p.start = p.position;
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
Core::Expected<void, ParticleError> StepKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                       const float* clock, IParticleTrigonometry* trigonometry,
                                                       const glm::vec2& wind)
{
    if (p.type == KalimaFlare)
    {
        if (p.subtype == FlareZero)
            StepFlareZero(p, random, wind);
        else if (p.subtype == FlareTrail)
        {
            // 0x5449e1..0x544a11: la luz por 0,9f y la escala por 0,95f.
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
        StepFanta(p);
    else
        StepWaterfall(p, random);
    return {};
}
} // namespace Client::World
