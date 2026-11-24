#include "World/SmokeParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "World/Ftol.h"
#include "World/IParticleTrigonometry.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::World
{
namespace
{
constexpr int FullTurn = 360;
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));  // 0x6b88d4 y 0x6b86c0, 0,01f
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));      // 0x6b87b8, 0,1f
constexpr double Fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));      // 0x6b8854, 0,2f
constexpr double Twentieth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));  // 0x6b8a48 y 0x6b8ad0, 0,05f
constexpr double FourFifths = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38, 0,8f
constexpr double Eighth = 0.125;                                                      // 0x6ba170
constexpr double Half = 0.5;                                                          // 0x6b882c
constexpr double Shrink = static_cast<double>(std::bit_cast<float>(0x3da3d70au));     // 0x6ba1ac, 0,08f

using Math::F32;

void ScaleLight(Particle& particle, double factor)
{
    for (int channel = 0; channel < 3; ++channel)
        particle.light[channel] = F32(static_cast<double>(particle.light[channel]) * factor);
}

// La luz del pedido queda en +0x60..+0x68 (0x5418b1, 0x54228c).
void KeepTheLight(Particle& particle)
{
    particle.rotationFactor = particle.light.x;
    particle.field64 = {particle.light.y, particle.light.z};
}

// 0x5418ca..0x541924 y 0x5422a5..0x5422ff: la escala ((rand() & 31) + 8) · escala · 0,01f, el ángulo x rand() % 360 y
// el giro _ftol(reloj) % 360.
void ShakeAtBirth(Particle& particle, Crt::CrtRandom& random, float clock)
{
    constexpr int ScaleMask = 31, ScaleBase = 8;
    const double argumentScale = static_cast<double>(particle.scale);
    particle.scale = F32((static_cast<double>((random.Next() & ScaleMask) + ScaleBase) * argumentScale) * Hundredth);
    particle.angles.x = static_cast<float>(random.Next() % FullTurn);
    particle.rotation = static_cast<float>(FtolLow(static_cast<double>(clock)) % FullTurn);
}

// 0x541c58..0x541c7d, 0x5424ff..0x542524 y 0x5426f2..0x542717: la velocidad vertical ((rand() & 31) + 60) · 0,1f.
void RiseAtBirth(Particle& particle, Crt::CrtRandom& random)
{
    constexpr int RiseMask = 31, RiseBase = 60;
    particle.verticalSpeed = F32(static_cast<double>((random.Next() & RiseMask) + RiseBase) * Tenth);
}

// 0x541981..0x541a00: vida 25, el giro y el ángulo x rand() % 360 y la partida (rand() % 50 − 25; rand() % 50 − 25;
// −(rand() % 50)), que la /36 usa como distancia a su dueño.
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

// 0x545ed6..0x545ef1: la velocidad vertical suma `rise` y la z la suma sin redondear; la escala crece 0,05f.
void RiseLikeTheBreath(Particle& particle, double rise)
{
    const double speed = static_cast<double>(particle.verticalSpeed) + rise;
    particle.verticalSpeed = F32(speed);
    particle.position.z = F32(speed + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) + Twentieth);
}

// 0x54658b..0x546616 (/19) y 0x546630..0x5466bb (/20): la velocidad vertical suma rand() % 20 · 0,002f, la x y la y
// (rand() % 100 − 20) · 0,01f, la z la velocidad y la escala rand() % 10 · 0,001f; el giro queda igual a la escala.
void Shake(Particle& particle, Crt::CrtRandom& random)
{
    constexpr double RiseStep = static_cast<double>(std::bit_cast<float>(0x3b03126fu)); // 0x6ba3a4, 0,002f
    constexpr double GrowStep = static_cast<double>(std::bit_cast<float>(0x3a83126fu)); // 0x6ba128, 0,001f
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

// 0x545ef9..0x545f63: con vida menor a 23, la velocidad vertical suma la escala · 0,1f y la z y la y la suman sin
// redondear, la escala baja 0,08f; si no, la posición es la del dueño (+0x40 → +0x10) más la partida. El giro suma
// 0,01f.
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

// 0x545f68..0x545fbf: la z y la y retroceden la dirección guardada (+0x68 y +0x64) por 0,8f, la escala crece 0,08f, el
// giro 0,01f y la luz se apaga por 0,9708738f.
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

// 0x546905..0x546952: la escala baja 0,05f y la luz se apaga por 0,9090909f; con la escala sin redondear en 0 o menos
// y el azul guardado en 0 o menos, se apaga (0x545360).
void StepTheShrinking(Particle& particle)
{
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)); // 0x6ba26c
    const double shrunk = static_cast<double>(particle.scale) - Twentieth;
    particle.scale = F32(shrunk);
    ScaleLight(particle, Fade);
    if (!(shrunk > 0.0) && !(particle.light.z > 0.0f))
        particle.alive = false;
}
// 0x542943..0x542a5f: la /0..2 guardan la luz, viven 55 (la /1, 30), el ángulo x (rand() & 3) − 2 y el giro
// _ftol(reloj) % 360; los demás subtipos salen por 0x54304c con el prefijo.
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
    particle.rotation = static_cast<float>(FtolLow(static_cast<double>(clock)) % FullTurn);
}

// 0x546a13..0x546aff: la /0 baja el rojo guardado 0,015f mientras pasa de 0,1f y otros 0,005f mientras el verde pasa
// de 0,1f; la /2 baja el verde 0,005f y el azul 0,015f con el mismo límite. Después la luz es vida · 0,0181818f por la
// guardada, la subida suma 0,2f (la /1, 0,1f) y la z la suma sin redondear, la escala crece 0,03f y el giro suma el
// ángulo x. Los demás subtipos no hacen nada.
void StepThePuff(Particle& particle)
{
    constexpr float Floor = std::bit_cast<float>(0x3dcccccdu);                           // 0x6b87b8, 0,1f
    constexpr double Drop = static_cast<double>(std::bit_cast<float>(0x3c75c28fu));      // 0x6b8a24, 0,015f
    constexpr double SmallDrop = static_cast<double>(std::bit_cast<float>(0x3ba3d70au)); // 0x6b89d8, 0,005f
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3c94f209u));      // 0x6ba374
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));    // 0x6b8bd4, 0,03f
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
// 0x546852..0x546899 (/28) y 0x54689e..0x5468d9 (/29): la z suma la subida; con vida 9 o más la escala baja 0,4f y si
// no crece por 1,2f; la /28 apaga la luz por 0,92f y la /29 la pone en 1 (el ecx de 0x54486e).
void StepTheStormMist(Particle& particle)
{
    constexpr std::int32_t ShrinkingLife = 9;
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu)); // 0x6b87b4, 0,4f
    constexpr double Swell = static_cast<double>(std::bit_cast<float>(0x3f99999au)); // 0x6b8bb0, 1,2f
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3f6b851fu));   // 0x6ba3cc, 0,92f
    particle.position.z = F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.position.z));
    const double scale = static_cast<double>(particle.scale);
    particle.scale = F32(particle.life >= ShrinkingLife ? scale - Brake : scale * Swell);
    if (particle.subtype == StormMistSubtype)
        ScaleLight(particle, Dim);
    else
        particle.light = glm::vec3(1.0f);
}

// 0x5468de..0x546900: la escala crece 0,15f, la luz queda en (1; 3; 1) y la z suma la subida.
void StepTheShortStormMist(Particle& particle)
{
    constexpr double Growth = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0,15f
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
    constexpr float MistScale = std::bit_cast<float>(0x40133333u);  // 0x5425ce, 2,3f
    constexpr float ShortScale = std::bit_cast<float>(0x3fe66666u); // 0x54266f, 1,8f
    constexpr float MistRise = 33.0f;                               // 0x5425e0, 0x42040000
    constexpr float ShortRise = 15.0f;                              // 0x542676, 0x41700000
    constexpr double AboveGround = 15.0;                            // 0x6b87bc, 15,0f
    constexpr float Sway = 5.0f;                                    // 0x6b87f8, 5,0f
    const bool shortMist = particle.subtype == ShortStormMistSubtype;
    // 0x5425c7..0x5425f6, 0x54261b..0x54264a y 0x542668..0x542697.
    particle.life = shortMist ? ShortLife : MistLife;
    particle.scale = shortMist ? ShortScale : MistScale;
    particle.verticalSpeed = shortMist ? ShortRise : MistRise;
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    // 0x5425f9, 0x54264d y 0x54269a: 0x5f93c0 con la x y la y de la partícula (edi es +0x14 desde 0x53feb8), retenida
    // hasta sumarle 15.
    particle.position.z = F32(terrainHeight(particle.position.x, particle.position.y) + AboveGround);
    // 0x54260a: la /28 se dibuja con Effect\PoundingBall.jpg (0x6ad).
    if (particle.subtype == StormMistSubtype)
        particle.texture = CinderParticle;
    if (!shortMist)
        return {};
    // 0x5426ae..0x5426c7: s = fsin(reloj) · 5 en PC53, retenido; la x y la y le suman s.
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
    constexpr float Ember = std::bit_cast<float>(0x3e99999au); // 0x541973, 0,3f
    constexpr double BackStep = 15.0;                          // 0x6b87bc
    if (particle.type == RisingPuffParticle)
    {
        CreateThePuff(particle, random, clock);
        return;
    }
    switch (particle.subtype)
    {
    case EmberSmokeSubtype:
        // 0x541c18..0x541c7d: vida 20, la escala ((rand() & 63) + 64) · 0,01f, el giro rand() % 360 y la subida.
        particle.life = EmberLife;
        particle.scale = F32(static_cast<double>((random.Next() & FadingMask) + FadingBase) * Hundredth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case GlowSmokeSubtype:
        // 0x5418b1..0x541924: guarda la luz, vida 12.
        KeepTheLight(particle);
        particle.life = GlowLife;
        ShakeAtBirth(particle, random, clock);
        return;
    case ShakingSmokeSubtype:
        // 0x54228c..0x5422ff: guarda la luz (su paso no la lee), vida 40.
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
        // 0x54184b..0x5418a9, el alta del aliento /0: vida 16, la escala ((rand() & 31) + 48) · 0,01f, el ángulo x
        // rand() % 360 y el giro _ftol(reloj) % 360.
        particle.life = BreathLife;
        particle.scale = F32(static_cast<double>((random.Next() & BreathMask) + BreathBase) * Hundredth);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(FtolLow(static_cast<double>(clock)) % FullTurn);
        return;
    case StoneSmokeSubtype:
    {
        // 0x542450..0x5424b7: vida 32, la escala (((rand() & 31) + 48) · 0,01f + 1) · escala, el ángulo x y el giro
        // como la /23.
        particle.life = StoneLife;
        const double argumentScale = static_cast<double>(particle.scale);
        particle.scale =
            F32((static_cast<double>((random.Next() & BreathMask) + BreathBase) * Hundredth + 1.0) * argumentScale);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(FtolLow(static_cast<double>(clock)) % FullTurn);
        return;
    }
    case FadingSmokeSubtype:
        // 0x54252c..0x542591: vida 25, la escala ((rand() & 63) + 64) · 0,01f, el giro rand() % 360 y la subida.
        particle.life = FadingLife;
        particle.scale = F32(static_cast<double>((random.Next() & FadingMask) + FadingBase) * Hundredth);
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case ShrinkingSmokeSubtype:
        // 0x5426cf..0x542717: la escala del pedido (la que ya tiene), vida 15, el giro rand() % 360 y la subida.
        particle.life = ShrinkingLife;
        particle.rotation = static_cast<float>(random.Next() % FullTurn);
        RiseAtBirth(particle, random);
        return;
    case TrailSmokeSubtype:
        // 0x54196c..0x54197a: la luz (1; 0,3f; 0).
        particle.light = glm::vec3(1.0f, Ember, 0.0f);
        TrailAtBirth(particle, random);
        return;
    case BackTrailSmokeSubtype:
    {
        // 0x541932..0x541967: la luz del pedido es una dirección: queda en +0x60..+0x68, la luz pasa a (1, 1, 1) y la
        // z y la y retroceden 15 por el azul y el verde (la x no).
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
    constexpr double Fortieth = static_cast<double>(std::bit_cast<float>(0x3ccccccdu));     // 0x6ba1fc, 0,025f
    constexpr double ThirtySecond = 0.03125;                                                // 0x6b8880
    constexpr double Ember = static_cast<double>(std::bit_cast<float>(0x3f19999au));        // 0x6b8a20, 0,6f
    constexpr double Ash = static_cast<double>(std::bit_cast<float>(0x3eb33333u));          // 0x6b8bc4, 0,35f
    constexpr double FadingGrowth = static_cast<double>(std::bit_cast<float>(0x3e19999au)); // 0x6b8ae8, 0,15f
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
        // 0x5462c8..0x5462f1: l = vida · 0,05f, retenido: la luz (l; l · 0,5; l · 0,1f), y la escala crece 0,09f
        // (0x6ba3ac).
        constexpr double EmberGrowth = static_cast<double>(std::bit_cast<float>(0x3db851ecu));
        const double ember = static_cast<double>(particle.life) * Twentieth;
        particle.light = glm::vec3(F32(ember), F32(ember * Half), F32(ember * Tenth));
        particle.scale = F32(static_cast<double>(particle.scale) + EmberGrowth);
        return {};
    }
    case GlowSmokeSubtype:
    {
        // 0x546171..0x5461a0: l = vida · 0,125; con l mayor a 1, l = 1 − l. La luz es l por la guardada y sigue como
        // el aliento.
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
        // 0x546573 (/19: vida · 0,025f · 0,5) y 0x54661e (/20: vida · 0,025f): la luz gris.
        double gray = static_cast<double>(particle.life) * Fortieth;
        if (particle.subtype == ShakingSmokeSubtype)
            gray *= Half;
        particle.light = glm::vec3(F32(gray));
        Shake(particle, random);
        return {};
    }
    case WarmBreathSubtype:
    {
        // 0x5460a0..0x5460bd: l = vida · 0,125, la luz (l · 0,1f; l; l · 0,6f) y sigue como el aliento.
        const double warm = static_cast<double>(particle.life) * Eighth;
        particle.light = glm::vec3(F32(warm * Tenth), F32(warm), F32(warm * Ember));
        RiseLikeTheBreath(particle, BreathRise);
        return {};
    }
    case StoneSmokeSubtype:
    {
        // 0x54675f..0x546790: l = vida · 0,03125, la luz (l · 0,2f; l · 0,5; l · 0,35f) y el aliento con 0,1f.
        const double ash = static_cast<double>(particle.life) * ThirtySecond;
        particle.light = glm::vec3(F32(ash * Fifth), F32(ash * Half), F32(ash * Ash));
        RiseLikeTheBreath(particle, Tenth);
        return {};
    }
    case FadingSmokeSubtype:
        // 0x5467ed..0x546815: la luz por vida · 0,05f y la escala crece 0,15f.
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

} // namespace Client::World
