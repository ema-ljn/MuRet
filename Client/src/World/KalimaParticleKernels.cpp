#include "World/KalimaParticleKernels.h"
#include "Core/Expected.h"

#include "World/Ftol.h"
#include "World/IParticleTrigonometry.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

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
// La 5ac/5 de la calavera 0x628/5 (0x52b843) y del empujón de la skill 0x28 (0x4fbd3a), y la 5ac/8 del 0x61e/6
// (0x52baf5).
constexpr int FlareFive = 5, FlareEight = 8;
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
// 0x540151..0x54018f: el 5ac/0 (y el /2 y el /8, que 0x53ffa5 y 0x53ffae mandan al mismo 0x540151) son las mismas
// instrucciones que el /6 (0x54003c); el /7 (0x53ffd9) vive 30 más y su escala es rand() % 15 + 30.
void CreateFlare(Particle& p, Render::Models::CrtRandom& random)
{
    const bool six = p.subtype != FlareSeven;
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
constexpr double KundunFallTurn = 4.0; // 0x6b8a44
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
// 0x544295..0x544318 (6f2/2, 6f3/2) y 0x543e14..0x543e97 (6f4/4), las salpicaduras del joint de Kundun: la vida 6, la
// subida (+0x50) ((rand() % 100) · 0,01f · 4 + ángulo x · 1,2f, sumada en la x87 en ese orden), la escala
// (rand() % 6) · paso + la del pedido, el giro y la partida en la posición. Tres rand().
void CreateKundunSplash(Particle& p, Render::Models::CrtRandom& random, double scaleStep)
{
    constexpr std::int32_t SplashLife = 6;
    constexpr int RiseSpan = 100, ScaleSpan = 6;
    constexpr double RiseFactor = 4.0;                                                    // 0x6b8a44
    constexpr double TiltFactor = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1,2f
    const float scaleArgument = p.scale;
    p.life = SplashLife;
    const double rise = static_cast<double>(random.Next() % RiseSpan) * Hundredth * RiseFactor;
    p.verticalSpeed = F32(static_cast<double>(p.angles.x) * TiltFactor + rise);
    p.scale = F32(static_cast<double>(random.Next() % ScaleSpan) * scaleStep + scaleArgument);
    p.rotation = static_cast<float>(random.Next() % FullTurn);
    p.start = p.position;
}
// 0x5480fd..0x548158 (6f2/2, 6f3/2) y 0x547f6b..0x547fa2 (6f4/4): x += cos(ángulo z) · 20 e y += sin(ángulo z) · 20
// con fcos/fsin del ángulo crudo (sin pasar a radianes), z += la subida y el giro + 1. La 6f4/4 sigue con el apagado
// de la 6f4 (0x547fc1); las otras multiplican la luz por 0,9090909f (0x6ba26c). Sin rand().
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
// 0x547a40..0x547cb4, la fanta con el dueño (+0x40) que no es nulo: la /1 y la /2 pierden dos de vida con su acción
// (+0x111) en 1 (0x547a4d); la /3 y la /4 buscan la mano izquierda o la derecha del Monster82 (0x547c1e, 0x4010b0) y,
// si está, la distancia d (las restas, los cuadrados y la raíz en la x87, ((dz² + dy²) + dx²)) achica la escala d ·
// 0,003f (0x6b89cc) y apaga la luz d · 0,08f (0x6ba1ac), que el paso común pisa después con la vida.
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
// 0x547cba..0x547d3d: la escala baja 0,02f (la /7, 0,1f y además sube 2), sin bajar de 0; la velocidad x e y por 0,95f,
// la z sube 1 y la luz es vida · 0,04f.
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
        // 0x547f58: la 6f4/3 además gira 4 entre la escala y la caída.
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
    // 0x543b82 y 0x547a40: todos los subtipos; la /1..4 leen a su dueño en el paso (0x547a4d y 0x547a73).
    const bool fanta = type == KalimaFanta || type == CrywolfFanta;
    return objectWaterfall || fanta ||
           (type == KalimaWaterfallLast && (subtype == 1 || subtype == KundunFallSubtype ||
                                            subtype == KundunSplashLastSubtype || subtype == BrazierFallSubtype)) ||
           ((type == WaterfallSplash || type == WaterfallSplash + 1) && subtype == KundunSplashSubtype) ||
           (type == KalimaWaterfallMist && subtype == BootsMistSubtype) ||
           (type == KalimaFlare &&
            (subtype == FlareZero || subtype == FlareTrail || subtype == FlareFive || subtype == FlareSix ||
             subtype == FlareSeven || subtype == FlareEight || subtype == FlareNine)) ||
           (subtype == 0 && type >= KalimaWaterfallFirst && type <= KalimaWaterfallLast);
}
Core::Expected<void, ParticleError> CreateKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                         std::optional<std::int32_t> scene)
{
    if (p.type == KalimaFlare)
    {
        // 0x54007f: el 5ac/1 (la estela del efecto 0x5ac de Icarus) sólo vive 50, sin rand(). 0x540140: el /5 vive
        // 25, sin rand(). 0x53ffb9: el 5ac/9 vive ftol(escala · 40), sin rand().
        constexpr std::int32_t TrailLife = 50, FiveLife = 25;
        constexpr double NineLifePerScale = 40.0;
        if (p.subtype == FlareTrail)
            p.life = TrailLife;
        else if (p.subtype == FlareFive)
            p.life = FiveLife;
        else if (p.subtype == FlareNine)
            p.life = FtolLow(static_cast<double>(p.scale) * NineLifePerScale);
        else
            CreateFlare(p, random);
        return {};
    }
    if (p.type == KalimaFanta || p.type == CrywolfFanta)
    {
        // 0x543b82..0x543be5: vida 24, los dos rand() de la velocidad y, con la /5, la vida en 20 (la /6, 32).
        // 0x543aed: la /7 vive 15, con la escala rand() % 30 · 0,01f más la del pedido y la posición corrida (rand() %
        // 10) · 0,1f − 0,5 en x, y y z, antes de la velocidad.
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
    if (p.type == KalimaWaterfallLast && p.subtype == KundunFallSubtype)
    {
        // 0x543dc0..0x543e0a: la vida 20, el giro, la escala 1,6f (0x3fcccccd) y la caída −7 − rand() % 5. A
        // diferencia de la 6f4/0, no toca la luz: queda la del pedido.
        constexpr int FallSpan = 5, FallBase = 7;
        p.life = ShortLife;
        p.rotation = static_cast<float>(random.Next() % FullTurn);
        p.scale = std::bit_cast<float>(0x3fcccccdu);
        p.velocity.z = static_cast<float>(-FallBase - random.Next() % FallSpan);
        return {};
    }
    if (p.type == KalimaWaterfallLast && p.subtype == KundunSplashLastSubtype)
    {
        // 0x543e14..0x543e97: como la 6f2/2, con la escala de 0,2f (0x6b8854) por paso.
        CreateKundunSplash(p, random, static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)));
        return {};
    }
    if (p.subtype == KundunSplashSubtype && (p.type == WaterfallSplash || p.type == WaterfallSplash + 1))
    {
        // 0x544295..0x544318: la escala de 0,1f (0x6b87b8) por paso.
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
Core::Expected<void, ParticleError> StepKalimaParticle(Particle& p, Render::Models::CrtRandom& random,
                                                       const float* clock, IParticleTrigonometry* trigonometry,
                                                       const glm::vec2& wind, const ParticleOwnerStates& ownerStates)
{
    if (p.type == KalimaFlare)
    {
        if (p.subtype == FlareZero || p.subtype == FlareEight)
        {
            // 0x5448e1..0x544903 y 0x544c09..0x544c19: el /8 entra con el cmp por 8 todavía igual y toma la k = 0,5f
            // del /0; después de su muerte, 0x544dd9..0x544dfd le pone la luz (escala; escala · 0,5; escala · 0,3f).
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
            // 0x5449e1..0x544a11 (el /5 por 0x544a19): la luz por 0,9f y la escala por 0,95f.
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
} // namespace Client::World
