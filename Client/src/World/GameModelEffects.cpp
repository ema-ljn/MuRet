#include "World/GameModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/KalimaWorld.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Ftol.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}
Core::Expected<void, std::string> StepKalimaEffect(ModelEffect& effect, KalimaWorld* kalima,
                                                   Render::Models::CrtRandom& random)
{
    constexpr double Acceleration = 3, RotationFactor = 5;
    constexpr float WaterPlane = 350;
    constexpr double TileFactor = static_cast<double>(0.01f);
    constexpr std::int32_t Radius = 2, Amplitude = -1000;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float delta = static_cast<float>(static_cast<double>(effect.speed) * effect.velocity[axis]);
        effect.transform.position[axis] =
            static_cast<float>(static_cast<double>(delta) + effect.transform.position[axis]);
    }
    const double retainedSpeed = static_cast<double>(effect.speed) + Acceleration;
    effect.speed = static_cast<float>(retainedSpeed);
    effect.transform.angles.x = static_cast<float>(retainedSpeed * RotationFactor + effect.transform.angles.x);
    if (!(effect.transform.position.z >= WaterPlane) && effect.disturbanceState == 0)
    {
        effect.disturbanceState = 1;
        const auto tileY = FtolLow(static_cast<double>(effect.transform.position.y) * TileFactor);
        const auto tileX = FtolLow(static_cast<double>(effect.transform.position.x) * TileFactor);
        if (kalima != nullptr)
            return kalima->Disturb(tileX, tileY, Radius, Amplitude, random);
    }
    return {};
}
Core::Expected<void, std::string> StepTower(ModelEffect& effect, float worldClock)
{
    // 525e9d..525efd: B guarda el argumento antes del seno; R y G retienen el producto PC53.
    constexpr std::array<float, 3> Factors{0.0011f, 0.0017f, 0.0013f};
    constexpr float Amplitude = 0.2f;
    constexpr double Bias = static_cast<double>(0.01f);
    for (int axis = 0; axis < 3; ++axis)
    {
        double argument =
            static_cast<double>(worldClock) * static_cast<double>(Factors[static_cast<std::size_t>(axis)]);
        if (axis == 2)
            argument = static_cast<double>(static_cast<float>(argument));
        const auto sine = Render::Models::Detail::SineProductPC53(argument, Amplitude);
        if (!sine)
            return Core::Unexpected(std::string("0x525ec4: el seno de Noria no cabe en el perfil PC53"));
        effect.light[axis] = static_cast<float>(*sine + Bias);
    }
    constexpr double PitchStep = 4.0;
    effect.transform.angles.y = static_cast<float>(static_cast<double>(effect.turnDirection) +
                                                   static_cast<double>(effect.transform.angles.y) + PitchStep);
    return {};
}
// 0x52973e: el plano 0x135 vuelve a vida 100 en cada paso y, con dueño (+0x108) y el byte vivo en 1 (siempre,
// porque el paso sólo corre con él), su z es (sen(T · 0,001f) + 1) · 200 más la z de la partida: fld T, fmul
// 0x6b89c8, fsin, fadd 0x6b87d4, fmul 0x6b8928, fadd +0x184 y un solo fstp.
Core::Expected<void, std::string> StepGatePlane(ModelEffect& effect, float worldClock)
{
    constexpr std::int32_t GatePlaneLife = 100;
    constexpr double ClockFactor = static_cast<double>(std::bit_cast<float>(0x3a83126fu)); // 0x6b89c8.
    constexpr double Lift = 200.0;                                                         // 0x6b8928.
    constexpr double One = 1.0;                                                            // 0x6b87d4.
    effect.life = GatePlaneLife;
    if (!effect.hasOwner)
        return {};
    const auto lifted = Render::Models::Detail::SineSumPC53(static_cast<double>(worldClock) * ClockFactor, One);
    if (!lifted)
        return Core::Unexpected(std::string("0x529768: el seno del plano 0x135 no cabe en el perfil PC53"));
    effect.transform.position.z = F32(*lifted * Lift + static_cast<double>(effect.start.z));
    return {};
}
Core::Expected<void, std::string> EmitTeleportSparks(const ModelEffect& effect, Particles& particles, float worldClock,
                                                     Render::Models::CrtRandom& random)
{
    constexpr int SparkCount = 18;
    constexpr double HeightStep = 24.0;
    constexpr double LightPerLife = static_cast<double>(0.1f);
    const glm::vec3 light(static_cast<float>(static_cast<double>(effect.life) * LightPerLife));
    glm::vec3 position = effect.transform.position;
    for (int spark = 0; spark < SparkCount; ++spark)
    {
        position.z = static_cast<float>(static_cast<double>(position.z) + HeightStep);
        const float scale = spark == 0 ? 12.0f : 6.0f;
        const auto created = particles.CreateChecked(SparkParticle, position, effect.transform.angles, light,
                                                     TeleportSparkSubtype, scale, worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(std::string("0x52ab6d: no se pudo crear la chispa de teleport"));
    }
    return {};
}
// 0x520d11..0x520fd1, con la luz local en (1, 1, 1) desde 0x520921.
Core::Expected<void, std::string> StepButterfly(ModelEffect& effect, Particles& particles, float worldClock,
                                                Render::Models::CrtRandom& random,
                                                const ModelEffectStepServices& services)
{
    constexpr int TurnSpan = 10, DriftSpan = 15, DriftCenter = 7;
    glm::vec3& position = effect.transform.position;
    glm::vec3& angles = effect.transform.angles;
    const int turn = random.Next() % TurnSpan;
    angles.z = effect.state > 0 ? F32(static_cast<double>(turn) + angles.z) : F32(static_cast<double>(angles.z) - turn);
    if ((random.Next() & 31) == 0)
        effect.velocity.z = static_cast<float>(random.Next() % DriftSpan - DriftCenter);
    constexpr double Flap = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));    // 0x6b8854.
    constexpr double Damping = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38.
    constexpr double Hop = static_cast<double>(std::bit_cast<float>(0x3e99999au));     // 0x6b89dc.
    effect.velocity.z = F32(static_cast<double>(random.Next() % DriftSpan - DriftCenter) * Flap + effect.velocity.z);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x520dad: la mariposa pide la altura del terreno (0x5f93c0)"));
    // 520db2..520e01: la altura queda en la pila para las dos comparaciones.
    const double height = services.terrainHeight(position.x, position.y);
    constexpr double Floor = 50.0, Ceiling = 150.0;
    if (height + Floor > static_cast<double>(position.z))
        effect.velocity.z = F32(static_cast<double>(effect.velocity.z) * Damping + 1.0);
    if (!(height + Ceiling >= static_cast<double>(position.z)))
        effect.velocity.z = F32(static_cast<double>(effect.velocity.z) * Damping - 1.0);
    position.z = F32(static_cast<double>(random.Next() % DriftSpan - DriftCenter) * Hop + position.z);
    const double level = static_cast<double>((random.Next() & 31) + 64) * static_cast<double>(0.01f);
    // 0x6b87b4, 0x6b8a38, 0x6b8a20 y, en el /3, 0x6b8a5c, 0x6b8a1c y 0x6b882c.
    constexpr std::array<std::array<float, 3>, 4> Tints{
        {{0.4f, 0.8f, 0.6f}, {0.4f, 0.6f, 0.8f}, {0.6f, 0.8f, 0.4f}, {0.7f, 0.9f, 0.5f}}};
    glm::vec3 light(1.0f);
    if (effect.subtype >= 0 && effect.subtype < static_cast<std::int32_t>(Tints.size()))
    {
        const auto& tint = Tints[static_cast<std::size_t>(effect.subtype)];
        for (int axis = 0; axis < 3; ++axis)
            light[axis] = F32(level * static_cast<double>(tint[static_cast<std::size_t>(axis)]));
    }
    constexpr std::int32_t SparkingSubtype = 3;
    if (effect.subtype == SparkingSubtype && (random.Next() & 1) == 0)
    {
        // 520f05..520fb1: los tres desplazamientos se sortean antes de sumarse.
        constexpr int SparkSpan = 15, SparkCenter = 8, SparkSubtype = 7;
        glm::vec3 offset{};
        for (int axis = 0; axis < 3; ++axis)
            offset[axis] = static_cast<float>(static_cast<int>(random.Next() & SparkSpan) - SparkCenter);
        glm::vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = F32(static_cast<double>(offset[axis]) + position[axis]);
        const auto created = particles.CreateChecked(FairySparkParticle, point, angles, effect.light, SparkSubtype,
                                                     1.0f, worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(std::string("0x520fb1: no se pudo crear la chispa 5c5/7 de la mariposa"));
    }
    if (services.sprites == nullptr)
        return Core::Unexpected(std::string("0x520fd1: la mariposa pide la tabla de sprites (0x5491e0)"));
    constexpr std::uint16_t Glow = 0x5ac;
    static_cast<void>(services.sprites->Create(Glow, position, 1.0f, light, 0.0f, 0));
    return {};
}
} // namespace

float StepLuminosity(int draw, std::int32_t life)
{
    constexpr int LightMask = 3, LightBase = 7;
    constexpr std::int32_t FadeLife = 5;
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    float light = F32(static_cast<double>((draw & LightMask) + LightBase) * Tenth);
    if (life < FadeLife)
    {
        const double faded = static_cast<double>(light) - (static_cast<double>(FadeLife - life) * Fifth);
        light = F32(faded);
        if (faded < 0.0)
            light = 0.0f;
    }
    return light;
}

namespace
{
// 0x52444d..0x5244ae: cae con +0xe4, que pierde 1 por paso; si el suelo (0x5f93c0, ordenado y por encima)
// la pasó, se apoya, rebota a −0,4f (0x6ba18c), pierde 4 de vida y 2 en la y de su velocidad.
Core::Expected<void, std::string> StepFallingStone(ModelEffect& effect, const ModelEffectStepServices& services)
{
    constexpr double Bounce = static_cast<double>(std::bit_cast<float>(0xbecccccdu));
    constexpr double Pull = 2.0;
    constexpr std::int32_t BounceLife = 4;
    glm::vec3& position = effect.transform.position;
    position.z = F32(static_cast<double>(position.z) + static_cast<double>(effect.turnDirection));
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - 1.0);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x524473: la piedra 0xda pide la altura del terreno (0x5f93c0)"));
    const double height = services.terrainHeight(position.x, position.y);
    if (height > static_cast<double>(position.z))
    {
        position.z = F32(height);
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) * Bounce);
        effect.life -= BounceLife;
        effect.velocity.y = F32(static_cast<double>(effect.velocity.y) - Pull);
    }
    return {};
}

// 0x5239c8..0x523b3a con el subtipo 0 y el 10: la velocidad sin girar sumada y por 0,9f (0x6b8a1c), la caída +0xe4
// que pierde 3; en el suelo se apoya, rebota a −0,5, pierde 4 de vida y se inclina escala · 128 (si no, ·
// 32); con el subtipo 0 y rand() % 10 en 0, un fuego 5d9 con el subtipo rand() % 3 + 1. El 10 (el de la piedra
// 0x118/0x119) sale en 0x523ad5 sin ese rand(). El 5 sigue a su dueño (y salta la cola) y el resto no está portado.
Core::Expected<void, std::string> StepStoneChip(ModelEffect& effect, Particles& particles, float worldClock,
                                                Render::Models::CrtRandom& random,
                                                const ModelEffectStepServices& services)
{
    if (effect.subtype != 0 && effect.subtype != GroundStoneChipSubtype)
        return Core::Unexpected(std::format("0x5238be: el paso de la piedrita {:#x} con el subtipo {} no está portado",
                                            effect.type, effect.subtype));
    constexpr double Drag = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double Fall = 3.0, Bounce = -0.5, GroundTilt = 128.0, AirTilt = 32.0;
    constexpr std::int32_t BounceLife = 4;
    constexpr int FireChance = 10, FireSubtypes = 3;
    glm::vec3& position = effect.transform.position;
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(static_cast<double>(effect.velocity[axis]) + static_cast<double>(position[axis]));
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Drag);
    position.z = F32(static_cast<double>(position.z) + static_cast<double>(effect.turnDirection));
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - Fall);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x523a80: la piedrita pide la altura del terreno (0x5f93c0)"));
    const double height = services.terrainHeight(position.x, position.y);
    double tilt = static_cast<double>(effect.transform.scale) * AirTilt;
    if (height > static_cast<double>(position.z))
    {
        position.z = F32(height);
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) * Bounce);
        effect.life -= BounceLife;
        tilt = static_cast<double>(effect.transform.scale) * GroundTilt;
    }
    effect.transform.angles.x = F32(static_cast<double>(effect.transform.angles.x) - tilt);
    if (effect.subtype == GroundStoneChipSubtype || random.Next() % FireChance != 0)
        return {};
    const int subtype = random.Next() % FireSubtypes + 1;
    const auto created = particles.CreateChecked(FireParticle, position, effect.transform.angles, effect.light, subtype,
                                                 1.0f, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x523b3a: no se pudo crear el fuego 5d9/{} de la piedrita", subtype));
    return {};
}

// 0x52ad21..0x52ae7a con el subtipo 0: seis llamas 5de/0 a ±25 en x e y con la luz local en blanco (los dos
// sorteos de cada una antes de su creación); con rand() & 7 en 0, la piedrita 0xc8 + (rand() & 1) con su
// luz; y la luz dinámica (L; L · 0,4f; 0) de radio 3. Después, 0x52ae2a: con el héroe publicado de dueño (+0x108, el
// chorro de la Flame; los de los objetos del 4 no tienen dueño) y la vida (antes de la cola) múltiplo de 20 (IDIV con
// signo), 0x517a20 a 150 (0x43160000), sin mirar lo que devuelve.
Core::Expected<void, std::string> StepFlameJet(ModelEffect& effect, float luminosity, Particles& particles,
                                               float worldClock, Render::Models::CrtRandom& random,
                                               const ModelEffectStepServices& services)
{
    if (effect.subtype != 0)
        return Core::Unexpected(
            std::format("0x52ae7f: el paso del chorro 0x5de con el subtipo {} no está portado", effect.subtype));
    constexpr int Flames = 6, Spread = 50, Center = 25, ChipMask = 7, ChipKind = 1;
    constexpr std::int32_t LightRadius = 3;
    constexpr double Orange = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    const glm::vec3& position = effect.transform.position;
    for (int flame = 0; flame < Flames; ++flame)
    {
        const auto dx = static_cast<float>(random.Next() % Spread - Center);
        const auto dy = static_cast<float>(random.Next() % Spread - Center);
        const glm::vec3 point{F32(static_cast<double>(dx) + static_cast<double>(position.x)),
                              F32(static_cast<double>(dy) + static_cast<double>(position.y)), position.z};
        const auto created = particles.CreateChecked(FlameParticle, point, effect.transform.angles, glm::vec3(1.0f), 0,
                                                     1.0f, worldClock, random);
        if (!created.HasValue())
            return Core::Unexpected(std::string("0x52ada0: no se pudo crear la llama 5de/0 del chorro"));
    }
    if ((random.Next() & ChipMask) == 0)
    {
        if (services.effects == nullptr)
            return Core::Unexpected(std::string("0x52adf2: el chorro 0x5de pide la tabla de efectos (0x518cf0)"));
        const auto type = static_cast<std::int16_t>(FirstStoneChipEffect + (random.Next() & ChipKind));
        static_cast<void>(
            services.effects->CreateStoneChip(type, position, effect.transform.angles, effect.light, random));
    }
    const glm::vec3 light{luminosity, F32(static_cast<double>(luminosity) * Orange), 0.0f};
    if (auto lit = AddDynamicLight(services.lighting, position, light, LightRadius, "0x52ae25"); !lit.HasValue())
        return lit;
    constexpr std::int32_t HitPeriod = 20;
    constexpr float HitRadius = 150.0f; // 0x43160000
    if (effect.ownerContext == nullptr || effect.ownerContext != services.hero || effect.life % HitPeriod != 0)
        return {};
    if (!services.skillHits)
        return Core::Unexpected(std::string("0x52ae72: el chorro 0x5de del héroe pide los blancos de 0x517a20"));
    return services.skillHits(effect, HitRadius);
}
// 0x5275ca..0x5276b7, las piedras 0x118 y 0x119 del pisotón del caballo. Con la vida entre 33 y 36, en p = (x + 60;
// y − 60; z + 50) (0x6b86cc y 0x6b86b0, cada eje en la x87 hasta su fstp): el polvo 5f2/11 con los ángulos del efecto,
// la luz (1; 0,8f; 0,6f), la escala 2 y el efecto de dueño (que ni su paso ni su dibujo leen: MuRet no lo guarda), y la
// piedrita 0xc8 + (rand() & 1) con el subtipo 10 y la luz del efecto. Con la vida bajo 8, el alpha por 0,7692308f
// (0x6ba240) y la luz de las mallas (+0x68) por 0,5f. Siempre, la luz dinámica (0,79f; 0,72f; 0,49f) de radio 2
// (0x52c4e5). Sin la tabla de efectos corta antes del rand() de la piedrita; sin la luz dinámica, al final.
Core::Expected<void, std::string> StepGroundStone(ModelEffect& effect, Particles& particles, float worldClock,
                                                  Render::Models::CrtRandom& random,
                                                  const ModelEffectStepServices& services)
{
    constexpr std::int32_t DustAfterLife = 32, DustBeforeLife = 37, FadeLife = 8;
    constexpr double Side = 60.0, Lift = 50.0;
    constexpr glm::vec3 DustLight{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)};
    constexpr float DustScale = 2.0f;
    constexpr std::int32_t ChipKind = 1;
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)); // 0x6ba240.
    constexpr double Dim = static_cast<double>(std::bit_cast<float>(0x3f000000u));  // 0x6b882c: 0,5f.
    constexpr glm::vec3 Glow{std::bit_cast<float>(0x3f4a3d71u), std::bit_cast<float>(0x3f3851ecu),
                             std::bit_cast<float>(0x3efae148u)};
    constexpr std::int32_t GlowRadius = 2;
    const glm::vec3& position = effect.transform.position;
    if (effect.life > DustAfterLife && effect.life < DustBeforeLife)
    {
        const glm::vec3 point{F32(static_cast<double>(position.x) + Side), F32(static_cast<double>(position.y) - Side),
                              F32(static_cast<double>(position.z) + Lift)};
        const auto dust = particles.CreateChecked(BreathParticle, point, effect.transform.angles, DustLight,
                                                  StoneDustSubtype, DustScale, worldClock, random);
        if (!dust.HasValue())
            return Core::Unexpected(std::format("0x527631: falló el polvo 5f2/11 de la piedra {:#x} ({})", effect.type,
                                                static_cast<int>(dust.Error())));
        if (services.effects == nullptr)
            return Core::Unexpected(
                std::format("0x52766a: la piedra {:#x} pide la tabla de efectos (0x518cf0)", effect.type));
        const auto type = static_cast<std::int16_t>(FirstStoneChipEffect + (random.Next() & ChipKind));
        static_cast<void>(
            services.effects->CreateGroundStoneChip(type, point, effect.transform.angles, effect.light, random));
    }
    if (effect.life < FadeLife)
    {
        effect.alpha = F32(static_cast<double>(effect.alpha) * Fade);
        effect.objectState.light = F32(static_cast<double>(effect.objectState.light) * Dim);
    }
    return AddDynamicLight(services.lighting, position, Glow, GlowRadius, "0x52c4f1");
}
// 0x52a737..0x52a7af con el subtipo 0: la velocidad sin girar sumada, la z de la velocidad 0,01f menos, una
// partícula 5ac/1 en el lugar con la luz y la escala del efecto y, con la z de la velocidad bajo −2 (o NaN),
// vida 0. La cola común suma la velocidad otra vez (los ángulos son 0).
Core::Expected<void, std::string> StepIcarusFlare(ModelEffect& effect, Particles& particles, float worldClock,
                                                  Render::Models::CrtRandom& random)
{
    if (effect.subtype != 0)
        return Core::Unexpected(
            std::format("0x52a7b7: el paso del 0x5ac con el subtipo {} no está portado", effect.subtype));
    constexpr double Sink = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    constexpr float Floor = -2.0f;
    constexpr std::int32_t TrailSubtype = 1;
    glm::vec3& position = effect.transform.position;
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(static_cast<double>(effect.velocity[axis]) + static_cast<double>(position[axis]));
    effect.velocity.z = F32(static_cast<double>(effect.velocity.z) - Sink);
    const auto created = particles.CreateChecked(IcarusFlareEffect, position, effect.transform.angles, effect.light,
                                                 TrailSubtype, effect.transform.scale, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x52a790: falló la partícula 5ac/1 del 0x5ac ({})", static_cast<int>(created.Error())));
    if (!(effect.velocity.z >= Floor))
        effect.life = 0;
    return {};
}
// FSIN y FMUL por un float de cualquier signo: el producto R53 del seno retenido por |f| con el signo de f
// (el redondeo al más cercano es simétrico). Con f en ±0, el cero con el signo del producto.
std::optional<double> SignedSineProductPC53(double argument, float multiplier)
{
    if (multiplier == 0.0f)
    {
        const auto unit = Render::Models::Detail::SineProductPC53(argument, 1.0f);
        if (!unit)
            return std::nullopt;
        return std::signbit(*unit) != std::signbit(multiplier) ? -0.0 : 0.0;
    }
    const auto product = Render::Models::Detail::SineProductPC53(argument, std::abs(multiplier));
    if (!product || multiplier > 0.0f)
        return product;
    return -*product;
}
// 0x520b77..0x520d0c: el 0xb3 de Aida. Con rand() % 40 en 1, el sentido +0xd8 se da vuelta y +0xe4 pasa a
// (rand() % 3 + 5) · sentido; con otro rand() % 40 en 1, igual con +0xec y rand() % 3 + 1. Lejos de la partida
// (la distancia de la x87, sin redondear, no menor que 500) y con el estado en 0, vida 50 y estado 1. El
// yaw suma sen(ftol(T) % 10.000 · 0,0001f) · +0xe4 y el pitch el mismo seno · +0xec · 1/3f; la luz es
// +0x68 · (0,1f; 0,1f; 0,25f) y suelta una 5ac/9 en el lugar con su escala, antes del avance de la cola.
Core::Expected<void, std::string> StepAidaWisp(ModelEffect& effect, Particles& particles, float worldClock,
                                               Render::Models::CrtRandom& random)
{
    constexpr int TurnChance = 40, TurnSpan = 3, YawBase = 5, PitchBase = 1;
    constexpr double Reverse = -1.0, Reach = 500.0;
    constexpr std::int32_t FarLife = 50;
    constexpr int Period = 10000;
    constexpr double Frequency = static_cast<double>(std::bit_cast<float>(0x38d1b717u)); // 0x6b8a94.
    constexpr double Third = static_cast<double>(std::bit_cast<float>(0x3eaaaaabu));     // 0x6ba2a0.
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));     // 0x6b8a34.
    constexpr double Quarter = 0.25;
    constexpr std::int32_t TrailSubtype = 9;
    if (random.Next() % TurnChance == 1)
    {
        effect.speed = F32(static_cast<double>(effect.speed) * Reverse);
        effect.turnDirection = F32(static_cast<double>(random.Next() % TurnSpan + YawBase) * effect.speed);
    }
    if (random.Next() % TurnChance == 1)
    {
        effect.speed = F32(static_cast<double>(effect.speed) * Reverse);
        effect.flagEc = F32(static_cast<double>(random.Next() % TurnSpan + PitchBase) * effect.speed);
    }
    glm::vec3& position = effect.transform.position;
    const double dx = static_cast<double>(position.x) - effect.start.x;
    const double dy = static_cast<double>(position.y) - effect.start.y;
    const double dz = static_cast<double>(position.z) - effect.start.z;
    // 0x520c27..0x520c37: dz², más dy², más dx², y la raíz; `test ah, 1` cuenta el NaN como "menor".
    const double distance = std::sqrt(((dz * dz) + (dy * dy)) + (dx * dx));
    if (distance >= Reach && effect.state == 0)
    {
        effect.life = FarLife;
        effect.state = 1;
    }
    const double argument = static_cast<double>(FtolLow(static_cast<double>(worldClock)) % Period) * Frequency;
    const auto yaw = SignedSineProductPC53(argument, effect.turnDirection);
    if (!yaw)
        return Core::Unexpected(std::string("0x520c86: el seno del 0xb3 no cabe en el perfil PC53"));
    glm::vec3& angles = effect.transform.angles;
    angles.z = F32(*yaw + static_cast<double>(angles.z));
    const auto pitch = SignedSineProductPC53(argument, effect.flagEc);
    if (!pitch)
        return Core::Unexpected(std::string("0x520cbb: el seno del 0xb3 no cabe en el perfil PC53"));
    angles.x = F32(*pitch * Third + static_cast<double>(angles.x));
    const double glow = static_cast<double>(effect.objectState.light);
    effect.light = {F32(glow * Tenth), F32(glow * Tenth), F32(glow * Quarter)};
    const auto created = particles.CreateChecked(IcarusFlareEffect, position, angles, effect.light, TrailSubtype,
                                                 effect.transform.scale, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x520d04: falló la partícula 5ac/9 del 0xb3 ({})", static_cast<int>(created.Error())));
    return {};
}

// 0x52b8a6..0x52b97f: el 0x61d. El /0 crea otro efecto (0x518cf0) y el /2 y el /7 sueltan órbitas 60f/0, que no
// salen de los objetos de los mapas portados; el resto no hace nada. El /3 (el del 0x38 de Crywolf), con la vida
// entre 6 y 9, suelta la 60f/10 con la luz del efecto, la escala 0,19 y el efecto como dueño, que la órbita /10
// no lee (Q-C346).
Core::Expected<void, std::string> StepFadingDecal(const ModelEffect& effect, Particles& particles,
                                                  const float& worldClock, Render::Models::CrtRandom& random)
{
    constexpr std::int32_t FirstEffectSubtype = 0, FirstOrbSubtype = 2, LastOrbSubtype = 7;
    constexpr std::int32_t OrbAfterLife = 5, OrbBeforeLife = 10;
    constexpr float OrbScale = std::bit_cast<float>(0x3e428f5cu); // 0x52b937: 0,19.
    if (effect.subtype == FirstEffectSubtype || effect.subtype == FirstOrbSubtype || effect.subtype == LastOrbSubtype)
        return Core::Unexpected(
            std::format("0x52b8a6: el paso del 0x61d con el subtipo {} no está portado", effect.subtype));
    if (effect.subtype != CrywolfDecalSubtype || effect.life <= OrbAfterLife || effect.life >= OrbBeforeLife)
        return {};
    PortableParticleTrigonometry trigonometry;
    const auto created =
        particles.CreateChecked(OrbitParticle, effect.transform.position, effect.transform.angles, effect.light,
                                AnchoredOrbitSubtype, OrbScale, std::cref(worldClock), random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x52b954: el alta de 0x60f/10 falla con error {}", static_cast<unsigned>(created.Error())));
    return {};
}

// 0x52c5ca..0x52c7a6, la onda 0x6eb. Con el subtipo 0 (la del pisotón del caballo) la escala pierde 1 (0x6b87d4),
// sin rand(). Con el 1 (la del galope en Icarus) la escala crece (rand() % 5) · 0,1f y la posición tiembla (rand() &
// 7) − 4 en x y en y, cada una en la x87 hasta su fstp: tres rand(). Después, una escala negativa vuelve a 0
// (0x52c74e) y, con el subtipo de 0 a 3, la luz es la vida, o 40 menos la vida pasados los 20, por 0,05f (0x6b8ad0)
// en los tres canales. Los demás subtipos cortan en su rama.
Core::Expected<void, std::string> StepShockWave(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    constexpr int GrowthSpan = 5;
    constexpr double GrowthStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b8a34: 0,1f.
    constexpr int ShakeSpan = 8;
    constexpr double ShakeCentre = static_cast<double>(std::bit_cast<float>(0x40800000u)); // 0x6b8a44: 4.
    constexpr std::int32_t FadeTurn = 20, FadeEnd = 40;
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8ad0: 0,05f.
    constexpr double Shrink = 1.0;                                                          // 0x6b87d4.
    if (effect.subtype == StompShockWaveSubtype)
        effect.transform.scale = F32(static_cast<double>(effect.transform.scale) - Shrink);
    else if (effect.subtype == GallopShockWaveSubtype)
    {
        const int growth = random.Next() % GrowthSpan;
        effect.transform.scale =
            F32((static_cast<double>(growth) * GrowthStep) + static_cast<double>(effect.transform.scale));
        for (int axis = 0; axis < 2; ++axis)
        {
            const int shake = random.Next() % ShakeSpan;
            effect.transform.position[axis] =
                F32((static_cast<double>(shake) - ShakeCentre) + static_cast<double>(effect.transform.position[axis]));
        }
    }
    else
        return Core::Unexpected(
            std::format("0x52c5ca: el paso del 0x6eb con el subtipo {} no está portado", effect.subtype));
    // fcomp con 0 y C0: también sin orden.
    if (!(effect.transform.scale >= 0.0f))
        effect.transform.scale = 0.0f;
    const std::int32_t fade = effect.life > FadeTurn ? FadeEnd - effect.life : effect.life;
    effect.light = glm::vec3(F32(static_cast<double>(fade) * LightPerLife));
    return {};
}

// 0x5295a4..0x529655, el rayo 0x14d de los Fenrir: con el subtipo 0 el alpha sube 0,3f (0x6b89dc) y, con la suma sin
// redondear (fst) en 1 o más, pasa al subtipo 1 con el alpha en 1; un alpha guardado negativo (o sin orden) vuelve a
// 1. Con el 1 baja 0,3f y, con la resta sin redondear en 0 o menos (o sin orden), se apaga con el alpha en 0. Con
// cualquier subtipo, los tres ángulos giran 0,15f (0x6b8ae8). Sin rand().
void StepLightning(ModelEffect& effect)
{
    constexpr double Flash = static_cast<double>(std::bit_cast<float>(0x3e99999au)); // 0x6b89dc: 0,3f.
    constexpr double Spin = static_cast<double>(std::bit_cast<float>(0x3e19999au));  // 0x6b8ae8: 0,15f.
    constexpr std::int32_t Lit = 1;
    if (effect.subtype == 0)
    {
        const double brighter = static_cast<double>(effect.alpha) + Flash;
        effect.alpha = F32(brighter);
        if (brighter >= 1.0)
        {
            effect.subtype = Lit;
            effect.alpha = 1.0f;
        }
        if (!(effect.alpha >= 0.0f))
            effect.alpha = 1.0f;
    }
    else if (effect.subtype == Lit)
    {
        const double dimmer = static_cast<double>(effect.alpha) - Flash;
        effect.alpha = F32(dimmer);
        if (!(dimmer > 0.0))
        {
            effect.alpha = 0.0f;
            effect.alive = false;
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        effect.transform.angles[axis] = F32(static_cast<double>(effect.transform.angles[axis]) + Spin);
}

// 0x52965a..0x529739, el calco 0x14e de los Fenrir: x gira 0,1f (0x6b87b8); la luz baja 0,05f (0x6b8a48) en el verde
// y el azul con el subtipo 1, en el rojo y el verde con el 2 y en el rojo y el azul con el 3; el alpha baja 0,05f y,
// con la resta sin redondear en 0 o menos (o sin orden), se apaga. Después, con más de 200 ms de timeGetTime() desde
// +0x1c0 (la resta y el jbe sin signo), el cuadro (+0x1c4) avanza, pasado el 4 se apaga, y +0x1c0 toma un
// timeGetTime() nuevo. Sin rand(); sin timeGetTime(), corta antes de tocarlo.
Core::Expected<void, std::string> StepLightningDecal(ModelEffect& effect, const ModelEffectStepServices& services)
{
    constexpr double Turn = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8: 0,1f.
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8a48: 0,05f.
    constexpr std::uint32_t FramePeriod = 200;
    constexpr std::int32_t LastFrame = 4;
    if (!services.systemTime)
        return Core::Unexpected(std::string("0x529701: falta timeGetTime() para el calco 0x14e de los Fenrir"));
    effect.transform.angles.x = F32(static_cast<double>(effect.transform.angles.x) + Turn);
    const auto fade = [&effect](int channel)
    { effect.light[channel] = F32(static_cast<double>(effect.light[channel]) - Fade); };
    if (effect.subtype == 1)
    {
        fade(1);
        fade(2);
    }
    else if (effect.subtype == 2)
    {
        fade(0);
        fade(1);
    }
    else if (effect.subtype == 3)
    {
        fade(0);
        fade(2);
    }
    const double left = static_cast<double>(effect.alpha) - Fade;
    effect.alpha = F32(left);
    if (!(left > 0.0))
        effect.alive = false;
    if (services.systemTime() - effect.textureTime > FramePeriod)
    {
        ++effect.textureFrame;
        if (effect.textureFrame > LastFrame)
            effect.alive = false;
        effect.textureTime = services.systemTime();
    }
    return {};
}

// El terremoto 0xfa y sus grietas 0xfb..0x102 (0x521c5c..0x522bd2).
// 0x6b86c0: el 0,01f de las celdas.
constexpr double QuakeCellScale = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
// El tipo de dueño que el terremoto y sus grietas tratan aparte (0x521f90, 0x522ab2 y 0x522b8a): sin las tres primeras
// grietas y con la luz azul. Ni el caballo ni el 0xfa lo tienen.
constexpr std::uint16_t SeparateOwnerType = 0x1b1;
// La altura de las grietas sobre el terreno: 3 (0x6b86b8), o 100 (0x6b86b4) en Kalima.
constexpr double CrackRise = 3.0, KalimaCrackRise = 100.0;
constexpr std::int32_t ExplosionSound = 0x40, QuakeSound = 0x97;

// 0x5fc8f0 y 0x5fcbd0: `offset` girado por AngleMatrix de `angles`.
glm::vec3 Turned(const glm::vec3& offset, const glm::vec3& angles)
{
    return Render::Models::OriginalVectorRotate(offset, Render::Models::OriginalAngleMatrix(angles));
}

// +0x108 distinto de 0 y su WORD +0x2, en vivo; nada sin dueño.
Core::Expected<std::optional<std::uint16_t>, std::string> LiveOwnerType(const ModelEffect& effect,
                                                                        std::string_view address)
{
    if (!effect.hasOwner)
        return std::optional<std::uint16_t>{};
    const std::optional<std::uint16_t> type = OwnerTypeOf(effect.owner);
    if (!type)
        return Core::Unexpected(std::format("{}: el efecto {:#x} lee el tipo (+0x2) de su dueño (+0x108) y no lo tiene",
                                            address, effect.type));
    return type;
}

// 0x6c9dd0 entre 24 y 29, o 36.
Core::Expected<bool, std::string> QuakeInKalima(const ModelEffectStepServices& services, std::string_view address)
{
    if (services.map == nullptr)
        return Core::Unexpected(std::format("{}: el terremoto lee el mapa (0x6c9dd0)", address));
    return IsKalimaMap(*services.map);
}

Core::Expected<double, std::string> QuakeHeight(const ModelEffectStepServices& services, float x, float y,
                                                std::string_view address)
{
    if (!services.terrainHeight)
        return Core::Unexpected(std::format("{}: el terremoto pide la altura del terreno (0x5f93c0)", address));
    return services.terrainHeight(x, y);
}

// 0x5f8290(ftol(x · 0,01f), ftol(y · 0,01f)) es y · 256 + x, sin la máscara de 0x5f82a0, y el WORD de 0x7d0d348 en ese
// lugar (el mismo lector que las piedras, UnmaskedCellAttribute): fuera de las 256 × 256 celdas el original lee fuera
// de la tabla, y MuRet corta (Q-C425).
Core::Expected<std::uint16_t, std::string> QuakeCellAttribute(const ModelEffectStepServices& services,
                                                              const glm::vec3& point, std::string_view address)
{
    if (services.terrainAttributes == nullptr)
        return Core::Unexpected(std::format("{}: el terremoto lee los atributos de las celdas (0x7d0d348)", address));
    return UnmaskedCellAttribute(services.terrainAttributes, point, address);
}

// 0x518cf0(tipo, punto, ángulos, luz, 0, dueño, tamaño, 0, 0, 0, 0) desde el paso del terremoto.
Core::Expected<void, std::string> CreateQuakeCrack(const ModelEffectStepServices& services, std::int16_t type,
                                                   const glm::vec3& point, const glm::vec3& angles,
                                                   const glm::vec3& light, const ModelEffectOwner& owner,
                                                   std::int16_t size, std::string_view address)
{
    if (services.effects == nullptr)
        return Core::Unexpected(std::format("{}: el terremoto pide la tabla de efectos (0x518cf0)", address));
    static_cast<void>(services.effects->CreateEarthquakeCrack(type, point, angles, light, 0, owner, size));
    return {};
}

// 0x521c8f..0x5221f0, la vida 11, con la luz local en blanco y los ángulos locales en 0.
Core::Expected<void, std::string> StepEarthquakeAtEleven(ModelEffect& effect, Particles& particles, float worldClock,
                                                         Render::Models::CrtRandom& random, KalimaWorld* kalima,
                                                         const ModelEffectStepServices& services)
{
    constexpr glm::vec3 Behind{-25.0f, -80.0f, 0.0f}; // 0x521cda..0x521cee.
    constexpr std::uint8_t Unanchored = 3, Anchored = 2;
    constexpr double Lift = 25.0, KalimaLift = 100.0;                   // 0x521d5a y 0x521d7e.
    constexpr double SinkBack = 27.0;                                   // 0x6b9ff8
    constexpr std::int32_t DisturbRadius = 2, DisturbAmplitude = -1000; // 0x521d93..0x521d98.
    constexpr float ExplosionScale = 0.5f;                              // 0x521dd8: 0x3f000000.
    constexpr std::int16_t FirstCrackSize = 0x96;
    const glm::vec3 white(1.0f);
    glm::vec3 local(0.0f);
    glm::vec3& start = effect.start;
    if (effect.state != Unanchored)
    {
        if (effect.owner.angles == nullptr)
            return Core::Unexpected(
                std::string("0x521cf8: el terremoto lee los ángulos (+0x1c) de su dueño (+0x108) y no los tiene"));
        const glm::vec3 behind = Turned(Behind, *effect.owner.angles);
        for (int axis = 0; axis < 3; ++axis)
            start[axis] = F32(static_cast<double>(behind[axis]) + static_cast<double>(effect.transform.position[axis]));
    }
    const auto inKalima = QuakeInKalima(services, "0x521d55");
    if (!inKalima.HasValue())
        return Core::Unexpected(inKalima.Error());
    double lift = Lift;
    if (inKalima.Value())
    {
        if (effect.state == 0 || effect.state == Anchored)
        {
            if (kalima == nullptr)
                return Core::Unexpected(std::string("0x521db2: el terremoto en Kalima pide sus ondas (0x437860)"));
            const std::int32_t tileY = FtolLow(static_cast<double>(start.y) * QuakeCellScale);
            const std::int32_t tileX = FtolLow(static_cast<double>(start.x) * QuakeCellScale);
            if (auto disturbed = kalima->Disturb(tileX, tileY, DisturbRadius, DisturbAmplitude, random);
                !disturbed.HasValue())
                return disturbed;
        }
        else
            lift = KalimaLift;
    }
    const auto ground = QuakeHeight(services, start.x, start.y, "0x521dce");
    if (!ground.HasValue())
        return Core::Unexpected(ground.Error());
    start.z = F32(ground.Value() + lift);
    const auto exploded =
        particles.CreateChecked(ExplosionParticle, start, local, white, 0, ExplosionScale, worldClock, random);
    if (!exploded.HasValue())
        return Core::Unexpected(std::format("0x521dfd: falló la explosión 5ed/0 del terremoto ({})",
                                            static_cast<unsigned>(exploded.Error())));
    if (exploded.Value().has_value())
    {
        if (!services.sound)
            return Core::Unexpected(std::string("0x542a82: la explosión 5ed pide el sonido 0x41ed10(0x40, 0, 0)"));
        services.sound(ExplosionSound);
    }
    if (effect.state == 0)
        return Core::Unexpected(
            std::string("0x521e13: el terremoto con el estado 0 (la habilidad del Dark Lord) suelta "
                        "joints 0x617, chispas 5c5 y el efecto 0x103, que no están portados"));
    start.z = F32(static_cast<double>(start.z) - SinkBack);
    const auto ownerType = LiveOwnerType(effect, "0x521f90");
    if (!ownerType.HasValue())
        return Core::Unexpected(ownerType.Error());
    if (ownerType.Value() && *ownerType.Value() != SeparateOwnerType)
    {
        // 0x521f98..0x521ffc: el dueño de las tres es este efecto (ebx).
        const ModelEffectOwner self{nullptr, &effect, &effect.transform.angles};
        for (const auto& [type, address] : {std::pair<std::int16_t, std::string_view>{std::int16_t{0xfd}, "0x521fb9"},
                                            std::pair<std::int16_t, std::string_view>{std::int16_t{0xfb}, "0x521fd9"},
                                            std::pair<std::int16_t, std::string_view>{std::int16_t{0xfc}, "0x521ffc"}})
        {
            if (auto created =
                    CreateQuakeCrack(services, type, start, local, effect.light, self, FirstCrackSize, address);
                !created.HasValue())
                return created;
        }
    }
    // 0x522004..0x5221ea: cinco rayos de a 72° más el subtipo.
    constexpr int Spokes = 5, ReachSpan = 150, ReachBase = 100, SizeSpan = 50, SizeBase = 40, TiltSpan = 30,
                  TiltCentre = 15;
    constexpr double SpokeAngle = 72.0; // 0x6b8ba0
    constexpr double Rise = 3.0;        // 0x6b86b8
    constexpr double Tilt = 45.0;       // 0x6b8848
    constexpr std::uint8_t Blocked = 0x4 | 0x8 | 0x10;
    for (int spoke = 0; spoke < Spokes; ++spoke)
    {
        const auto reach = static_cast<float>(random.Next() % ReachSpan + ReachBase);
        local = {
            0.0f, 0.0f,
            F32(static_cast<double>(spoke) * SpokeAngle + static_cast<double>(effect.subtype))}; // fild/fmul/fiadd.
        const glm::vec3 ray = Turned({0.0f, reach, 0.0f}, local);
        glm::vec3 point{F32(static_cast<double>(ray.x) + static_cast<double>(start.x)),
                        F32(static_cast<double>(ray.y) + static_cast<double>(start.y)),
                        F32((static_cast<double>(ray.z) + static_cast<double>(start.z)) + Rise)};
        const auto size = static_cast<std::int16_t>(random.Next() % SizeSpan + SizeBase);
        local = {0.0f, 0.0f, F32(static_cast<double>(random.Next() % TiltSpan - TiltCentre) + Tilt)};
        const auto attribute = QuakeCellAttribute(services, point, "0x52211d");
        if (!attribute.HasValue())
            return Core::Unexpected(attribute.Error());
        // 0x52212d..0x52214d: cualquiera de los tres bits del byte bajo la deja sin grieta.
        if ((static_cast<std::uint8_t>(attribute.Value()) & Blocked) != 0)
            continue;
        const auto kalimaHere = QuakeInKalima(services, "0x522153");
        if (!kalimaHere.HasValue())
            return Core::Unexpected(kalimaHere.Error());
        const auto height = QuakeHeight(services, point.x, point.y, "0x52217c");
        if (!height.HasValue())
            return Core::Unexpected(height.Error());
        point.z = F32(height.Value() + (kalimaHere.Value() ? KalimaCrackRise : CrackRise));
        for (const auto& [type, address] : {std::pair<std::int16_t, std::string_view>{std::int16_t{0xfe}, "0x5221b0"},
                                            std::pair<std::int16_t, std::string_view>{std::int16_t{0xff}, "0x5221d8"}})
        {
            if (auto created =
                    CreateQuakeCrack(services, type, point, local, effect.light, effect.owner, size, address);
                !created.HasValue())
                return created;
        }
    }
    return {};
}

// 0x5221fe..0x522493, la vida 10: cuatro anillos de cinco grietas que salen de la partida.
Core::Expected<void, std::string> StepEarthquakeAtTen(ModelEffect& effect, Render::Models::CrtRandom& random,
                                                      const ModelEffectStepServices& services)
{
    constexpr int Rings = 4, LastSteadyRing = 2, StepSpan = 15, PushSpan = 30, PushBase = 50, TurnSpan = 10,
                  TurnBase = 62;
    constexpr std::size_t Cracks = 5;
    constexpr double StepBase = 85.0; // 0x6ba104
    constexpr double Flip = 270.0;    // 0x6ba10c
    constexpr std::int16_t CrackSize = 100;
    constexpr std::uint8_t Blocked = 0x4 | 0x8 | 0x10;
    std::array<glm::vec3, Cracks> points{};
    points.fill(effect.start);
    std::array<float, Cracks> sums{};
    glm::vec3 local(0.0f);
    // ebp+8: 0, 1 y 2 en los tres primeros anillos; en el cuarto, un rand() nuevo (Q-C429).
    int parity = 0;
    for (int ring = 0; ring < Rings; ++ring)
    {
        const float reach = F32(static_cast<double>(random.Next() % StepSpan) + StepBase);
        if (ring > LastSteadyRing)
            parity = random.Next();
        const bool outward = parity % 2 == 0;
        for (std::size_t crack = 0; crack < Cracks; ++crack)
        {
            const int push = outward ? random.Next() % PushSpan + PushBase : -PushBase - random.Next() % PushSpan;
            sums[crack] = F32(static_cast<double>(push) + static_cast<double>(sums[crack]));
            const int turn = (random.Next() % TurnSpan + TurnBase) * static_cast<int>(crack);
            local.z = F32(static_cast<double>(turn) + static_cast<double>(sums[crack]));
            const glm::vec3 step = Turned({0.0f, reach, 0.0f}, local);
            glm::vec3& point = points[crack];
            for (int axis = 0; axis < 3; ++axis)
                point[axis] = F32(static_cast<double>(step[axis]) + static_cast<double>(point[axis]));
            const auto attribute = QuakeCellAttribute(services, point, "0x522378");
            if (!attribute.HasValue())
                return Core::Unexpected(attribute.Error());
            // 0x522388..0x5223a0: sólo los tres bits juntos la dejan sin grieta (en la vida 11, cualquiera; Q-C424).
            if ((static_cast<std::uint8_t>(attribute.Value()) & Blocked) == Blocked)
                continue;
            const auto kalimaHere = QuakeInKalima(services, "0x5223a6");
            if (!kalimaHere.HasValue())
                return Core::Unexpected(kalimaHere.Error());
            const auto height = QuakeHeight(services, point.x, point.y, "0x5223c0");
            if (!height.HasValue())
                return Core::Unexpected(height.Error());
            point.z = F32(height.Value() + (kalimaHere.Value() ? KalimaCrackRise : CrackRise));
            local.z = F32(static_cast<double>(local.z) + Flip);
            for (const auto& [type, address] :
                 {std::pair<std::int16_t, std::string_view>{std::int16_t{0x101}, "0x522417"},
                  std::pair<std::int16_t, std::string_view>{std::int16_t{0x102}, "0x522440"}})
            {
                if (auto created =
                        CreateQuakeCrack(services, type, point, local, effect.light, effect.owner, CrackSize, address);
                    !created.HasValue())
                    return created;
            }
        }
        ++parity;
    }
    effect.life = 0;
    if (!services.sound)
        return Core::Unexpected(std::string("0x52248b: el terremoto pide el sonido 0x41ed10(0x97, 0, 0)"));
    services.sound(QuakeSound);
    return {};
}

// 0x522498..0x522765, las demás vidas: se hamaca. La fase es la vida, o 12,5 entre 10 y 15 (con 18 de paso en vez de
// 15, y en la 15 +0xe4 cambia de signo); el pitch suma 80; la y de la velocidad es sen((20 − fase) · paso · 3,141592 ·
// 1/180) · 260 en la x87 (los dos double de 0x6ba288 y 0x6ba280); +0xe4 baja 8 con la fase en 12,5 (comparada con
// el double 12,5) y si no sube 8; y la posición es la partida más la velocidad girada por +0x28 (no por +0x1c), con
// la z más +0xe4 y 200 sin redondear la suma de en medio (fst).
Core::Expected<void, std::string> SwingTheEarthquake(ModelEffect& effect)
{
    constexpr std::int32_t FirstSlowLife = 10, LastSlowLife = 15, DarkLordLife = 13;
    constexpr float SlowPhase = 12.5f, SlowPace = 18.0f, Pace = 15.0f;              // 0x5224b2, 0x5224b9 y 0x52249e.
    constexpr double Top = 20.0;                                                    // 0x6b86d0
    constexpr double Pi = std::bit_cast<double>(0x400921fafc8b007aull);             // 0x6ba288: 3,141592.
    constexpr double InverseDegrees = std::bit_cast<double>(0x3f76c16c16c16c17ull); // 0x6ba280.
    constexpr double SlowPhaseDouble = 12.5;                                        // 0x6ba278
    constexpr float Swing = 260.0f;                                                 // 0x6b88f0
    constexpr double Pitch = 80.0;                                                  // 0x6b9ffc
    constexpr double Bob = 8.0;                                                     // 0x6b86c4
    constexpr double Rise = 200.0;                                                  // 0x6b8928
    auto phase = static_cast<float>(effect.life);
    float pace = Pace;
    if (effect.life >= FirstSlowLife && effect.life <= LastSlowLife)
    {
        phase = SlowPhase;
        pace = SlowPace;
        if (effect.life == LastSlowLife)
            effect.turnDirection = F32(static_cast<double>(effect.turnDirection) * -1.0); // 0x6b8b9c
    }
    if (effect.state == 0 && effect.life == DarkLordLife)
        return Core::Unexpected(std::string("0x5224ec: el terremoto con el estado 0 en la vida 13 (la habilidad del "
                                            "Dark Lord) suelta los 0x104 y el sonido 0x96, que no están portados"));
    glm::vec3& angles = effect.transform.angles;
    angles.x = F32(static_cast<double>(angles.x) + Pitch);
    const double argument = (((Top - static_cast<double>(phase)) * static_cast<double>(pace)) * Pi) * InverseDegrees;
    const auto swing = Render::Models::Detail::SineProductPC53(argument, Swing);
    if (!swing)
        return Core::Unexpected(std::string("0x522696: el seno del terremoto no cabe en el perfil PC53"));
    effect.velocity.y = F32(*swing);
    const glm::vec3& start = effect.start;
    glm::vec3& position = effect.transform.position;
    position = start;
    // 0x5226a4..0x5226dd: igual (ni C0 ni C3 sin C2) baja; menor, mayor o sin orden sube.
    const double bob = static_cast<double>(phase) == SlowPhaseDouble ? -Bob : Bob;
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) + bob);
    const glm::vec3 moved = Turned(effect.velocity, effect.copiedAngles);
    position.x = F32(static_cast<double>(moved.x) + static_cast<double>(start.x));
    position.y = F32(static_cast<double>(moved.y) + static_cast<double>(start.y));
    position.z = F32(
        ((static_cast<double>(moved.z) + static_cast<double>(start.z)) + static_cast<double>(effect.turnDirection)) +
        Rise);
    return {};
}

Core::Expected<void, std::string> StepEarthquake(ModelEffect& effect, Particles& particles, float worldClock,
                                                 Render::Models::CrtRandom& random, KalimaWorld* kalima,
                                                 const ModelEffectStepServices& services)
{
    constexpr std::int32_t BurstLife = 11, RingLife = 10;
    if (effect.life == BurstLife)
        return StepEarthquakeAtEleven(effect, particles, worldClock, random, kalima, services);
    if (effect.life == RingLife)
        return StepEarthquakeAtTen(effect, random, services);
    return SwingTheEarthquake(effect);
}

// 0x522a9c..0x522af7 (y 0x522b74..0x522bcd → 0x5212ce): +0x6c en vida · −0,01f (0x6b8a58) y la luz dinámica de radio
// 1: con dueño, (0; 0; L) si es del tipo 0x1b1 y (L; 0; 0) si no; sin dueño, el blanco del prólogo (Q-C427).
Core::Expected<void, std::string> LightTheCrack(ModelEffect& effect, float luminosity,
                                                const ModelEffectStepServices& services, std::string_view ownerAddress,
                                                std::string_view lightAddress)
{
    constexpr double Drift = static_cast<double>(std::bit_cast<float>(0xbc23d70au));
    constexpr std::int32_t Radius = 1;
    effect.objectState.uv.x = F32(static_cast<double>(effect.life) * Drift);
    const auto ownerType = LiveOwnerType(effect, ownerAddress);
    if (!ownerType.HasValue())
        return Core::Unexpected(ownerType.Error());
    glm::vec3 color(1.0f);
    if (ownerType.Value())
        color = *ownerType.Value() == SeparateOwnerType ? glm::vec3(0.0f, 0.0f, luminosity)
                                                        : glm::vec3(luminosity, 0.0f, 0.0f);
    return AddDynamicLight(services.lighting, effect.transform.position, color, Radius, lightAddress);
}

// 0x52284f..0x522a94 (y 0x522995..0x522a94): la piedrita 0xc8 + (rand() & 1) en la posición más (0; rand() % 150; 0)
// girado por (0; 0; rand() % 360), con los ángulos y la luz de la grieta: los tres rand() antes del alta.
Core::Expected<void, std::string> DropACrackChip(const ModelEffect& effect, Render::Models::CrtRandom& random,
                                                 const ModelEffectStepServices& services, std::string_view address)
{
    constexpr int ReachSpan = 150, Degrees = 360, Kinds = 2;
    const glm::vec3 reach{0.0f, static_cast<float>(random.Next() % ReachSpan), 0.0f};
    const glm::vec3 turn{0.0f, 0.0f, static_cast<float>(random.Next() % Degrees)};
    const glm::vec3 offset = Turned(reach, turn);
    glm::vec3 point{};
    for (int axis = 0; axis < 3; ++axis)
        point[axis] = F32(static_cast<double>(offset[axis]) + static_cast<double>(effect.transform.position[axis]));
    const auto type = static_cast<std::int16_t>(FirstStoneChipEffect + random.Next() % Kinds);
    if (services.effects == nullptr)
        return Core::Unexpected(
            std::format("{}: la grieta {:#x} pide la tabla de efectos (0x518cf0)", address, effect.type));
    static_cast<void>(services.effects->CreateStoneChip(type, point, effect.transform.angles, effect.light, random));
    return {};
}

// 0x52276a..0x522bd2, con L, la luz local del primer rand(). +0x68 es la luz de las mallas y la z baja 0,5f (0x6b882c).
Core::Expected<void, std::string> StepEarthquakeCrack(ModelEffect& effect, float luminosity,
                                                      Render::Models::CrtRandom& random,
                                                      const ModelEffectStepServices& services)
{
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b87b8 y 0x6b8a34.
    constexpr double Third = static_cast<double>(std::bit_cast<float>(0x3eaaaaabu)); // 0x6ba2a0
    constexpr double Sink = 0.5;
    constexpr float Wide = 50.0f; // 0x6b86b0
    constexpr std::int32_t Faded = 5, ShakeEvery = 3, ShakeAfter = 15, ShakeSpan = 8, ShakeCentre = 4;
    float& glow = effect.objectState.light;
    glm::vec3& position = effect.transform.position;
    const auto sink = [&position] { position.z = F32(static_cast<double>(position.z) - Sink); };
    const std::int32_t life = effect.life;
    switch (effect.type)
    {
    case FirstEarthquakeCrack: // 0x52276a
        glow = F32(static_cast<double>(life) * Tenth * Third);
        if (life < 10)
            sink();
        if (life > ShakeAfter && life % ShakeEvery == 0)
        {
            const int shake = random.Next() % ShakeSpan - ShakeCentre;
            if (services.cameraShake == nullptr)
                return Core::Unexpected(
                    std::string("0x5227ca: la grieta 0xfb tiembla la cámara (0x7d325c8) y no la tiene"));
            *services.cameraShake = F32(static_cast<double>(shake) * Tenth);
        }
        return {};
    case 0xfd:  // 0x5227d5
    case 0x100: // 0x5227d5
        glow = F32(static_cast<double>(life) * Tenth * Tenth);
        if (life < 13)
            sink();
        return {};
    case 0xfe:  // 0x522b04
    case 0x101: // 0x522b04
        glow = F32(static_cast<double>(life) * Tenth * Third);
        if (life < 10)
            sink();
        return {};
    case 0xfc: // 0x5227f0
    {
        // fcomp con 50: sólo mayor (ni menor, ni igual, ni sin orden) apaga la vida.
        if (effect.transform.scale > Wide)
            effect.life = 0;
        const std::int32_t left = effect.life;
        constexpr std::int32_t Bright = 10, Peak = 20, ChipChance = 10;
        if (left >= Bright)
            glow = F32(static_cast<double>(Peak - left) * Tenth);
        else
        {
            if (left < Faded)
                sink();
            glow = F32(static_cast<double>(left) * Tenth);
            if (left >= Faded && random.Next() % ChipChance == 0)
            {
                if (auto chipped = DropACrackChip(effect, random, services, "0x522a94"); !chipped.HasValue())
                    return chipped;
            }
        }
        return LightTheCrack(effect, luminosity, services, "0x522ab2", "0x522af7");
    }
    case 0xff: // 0x52294c
    {
        constexpr std::int32_t Bright = 30, Peak = 40, Low = 15, ChipChance = 15;
        if (life >= Bright)
            glow = F32(static_cast<double>(Peak - life) * Tenth);
        else
        {
            if (life < Low)
                sink();
            glow = F32(static_cast<double>(life) * Tenth);
            if (life >= Faded && random.Next() % ChipChance == 0)
            {
                if (auto chipped = DropACrackChip(effect, random, services, "0x522a94"); !chipped.HasValue())
                    return chipped;
            }
        }
        return LightTheCrack(effect, luminosity, services, "0x522ab2", "0x522af7");
    }
    default: // 0x102, 0x522b31.
    {
        constexpr std::int32_t Bright = 30, Peak = 40, Low = 15, Noisy = 13;
        glow = F32(static_cast<double>(life >= Bright ? Peak - life : life) * Tenth);
        if (life < Low)
            sink();
        // 0x522b6f: un rand() que nadie usa (Q-C428).
        if (life >= Noisy)
            static_cast<void>(random.Next());
        return LightTheCrack(effect, luminosity, services, "0x522b8a", "0x5212da");
    }
    }
}

// 0x52531d..0x525433 y 0x5252e6..0x525318: las piedras 0x10a y 0x10b. La posición más la velocidad (sin girar), la
// velocidad por 0,9f (0x6b8a1c), la z más la caída +0xe4 y, con el subtipo 1, la velocidad por 1,1f (0x6b8830); la
// caída menos 3. Con la altura del terreno (0x5f93c0) por encima de la z (FCOM: igual o NaN no), la z queda en ella,
// la caída por −0,2f (0x6b8d3c), cinco vidas menos y la x de los ángulos menos escala · 128; si no, menos escala · 16.
// +0x174 = vida · 0,1f. En 0x5252e6, rand() % 10 y, con 0, 0x53fe70(0x5f3, [EBP − 0x28], ángulos, (1; 1; 1), 0,
// 1,0f, 0): ese punto es un local de 0x5208a0 que ningún camino escribe (Q-C1176), así que corta ahí, después de ese
// rand() y antes del polvo y de la cola (P-C232, para la VM).
Core::Expected<void, std::string> StepDebris(ModelEffect& effect, Render::Models::CrtRandom& random,
                                             const ModelEffectStepServices& services)
{
    glm::vec3& position = effect.transform.position;
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(static_cast<double>(effect.velocity[axis]) + static_cast<double>(position[axis]));
    constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double Boost = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu));
    constexpr double Bounce = static_cast<double>(std::bit_cast<float>(0xbe4ccccdu));
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Gravity = 3.0, GroundTilt = 128.0, AirTilt = 16.0;
    constexpr std::int32_t BoostedSubtype = 1;
    constexpr std::int32_t GroundWear = 5;
    for (int axis = 0; axis < 3; ++axis)
        effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Brake);
    position.z = F32(static_cast<double>(position.z) + static_cast<double>(effect.turnDirection));
    if (effect.subtype == BoostedSubtype)
    {
        for (int axis = 0; axis < 3; ++axis)
            effect.velocity[axis] = F32(static_cast<double>(effect.velocity[axis]) * Boost);
    }
    effect.turnDirection = F32(static_cast<double>(effect.turnDirection) - Gravity);
    if (!services.terrainHeight)
        return Core::Unexpected(std::string("0x5253d9: la piedra 0x10b pide la altura del terreno (0x5f93c0)"));
    const double ground = services.terrainHeight(position.x, position.y);
    float& pitch = effect.transform.angles.x;
    if (ground > static_cast<double>(position.z))
    {
        position.z = F32(ground);
        effect.turnDirection = F32(static_cast<double>(effect.turnDirection) * Bounce);
        effect.life -= GroundWear;
        pitch = F32(static_cast<double>(pitch) - static_cast<double>(effect.transform.scale) * GroundTilt);
    }
    else
    {
        pitch = F32(static_cast<double>(pitch) - static_cast<double>(effect.transform.scale) * AirTilt);
    }
    effect.alpha = F32(static_cast<double>(effect.life) * Tenth);
    // 0x5252e6.
    constexpr int DustChance = 10;
    if (random.Next() % DustChance != 0)
        return {};
    return Core::Unexpected(std::format("0x5252e6: el polvo 0x5f3/0 de la piedra {:#x} va en [EBP − 0x28] de "
                                        "0x5208a0, un local que ningún camino escribe (Q-C1176; la VM, P-C232)",
                                        effect.type));
}
} // namespace
Core::Expected<void, std::string> StepGameModelEffect(ModelEffect& effect, const Render::Models::Skeleton* skeleton,
                                                      Particles& particles, float worldClock,
                                                      Render::Models::CrtRandom& random, KalimaWorld* kalima,
                                                      const ModelEffectStepServices& services)
{
    if (!effect.alive)
        return {};
    // 5208c5: el primer sorteo corre para todo tipo vivo, incluso si su rama no usa la luz local.
    const int lightDraw = random.Next();
    const glm::vec3 before = effect.transform.position;
    bool wholeStep = false;
    constexpr std::int16_t FirstTower = 322;
    constexpr std::int16_t LastTower = 324;
    constexpr std::int16_t KalimaType = 9;
    if (effect.type == KalimaType)
    {
        if (auto stepped = StepKalimaEffect(effect, kalima, random); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type >= FirstTower && effect.type <= LastTower)
    {
        if (auto stepped = StepTower(effect, worldClock); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == TeleportEffect)
    {
        if (auto emitted = EmitTeleportSparks(effect, particles, worldClock, random); !emitted.HasValue())
            return emitted;
    }
    else if (effect.type == ButterflyEffect)
    {
        if (auto stepped = StepButterfly(effect, particles, worldClock, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == FallingStoneEffect)
    {
        wholeStep = true;
        if (auto stepped = StepFallingStone(effect, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == FirstStoneChipEffect || effect.type == SecondStoneChipEffect)
    {
        wholeStep = true;
        if (auto stepped = StepStoneChip(effect, particles, worldClock, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == FirstGroundStoneEffect || effect.type == SecondGroundStoneEffect)
    {
        if (auto stepped = StepGroundStone(effect, particles, worldClock, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == FlameJetEffect)
    {
        wholeStep = true;
        const float luminosity = StepLuminosity(lightDraw, effect.life);
        if (auto stepped = StepFlameJet(effect, luminosity, particles, worldClock, random, services);
            !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == IcarusFlareEffect)
    {
        if (auto stepped = StepIcarusFlare(effect, particles, worldClock, random); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == AidaWispEffect)
    {
        if (auto stepped = StepAidaWisp(effect, particles, worldClock, random); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == GatePlaneEffect)
    {
        if (auto stepped = StepGatePlane(effect, worldClock); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == FadingDecalEffect)
    {
        if (auto stepped = StepFadingDecal(effect, particles, worldClock, random); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == ShockWaveEffect)
    {
        if (auto stepped = StepShockWave(effect, random); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == LightningEffect)
    {
        StepLightning(effect);
    }
    else if (effect.type == LightningDecalEffect)
    {
        if (auto stepped = StepLightningDecal(effect, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type == EarthquakeEffect)
    {
        if (auto stepped = StepEarthquake(effect, particles, worldClock, random, kalima, services); !stepped.HasValue())
            return stepped;
        // 0x52cbb0: el 0xfa salta la animación y 0x51fc80; sólo pierde una vida y, con 0 o menos, muere.
        effect.previousPosition = before;
        --effect.life;
        if (effect.life <= 0)
            effect.alive = false;
        return {};
    }
    else if (effect.type == FirstDebrisEffect || effect.type == DebrisEffect)
    {
        wholeStep = true;
        if (auto stepped = StepDebris(effect, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type >= FirstEarthquakeCrack && effect.type <= LastEarthquakeCrack)
    {
        wholeStep = true;
        const float luminosity = StepLuminosity(lightDraw, effect.life);
        if (auto stepped = StepEarthquakeCrack(effect, luminosity, random, services); !stepped.HasValue())
            return stepped;
    }
    else if (effect.type != DragonEffect && effect.type != ArrivalEffect && effect.type != IcarusCloudEffect)
        return Core::Unexpected(std::string("0x5208a0: el paso del tipo de efecto con modelo no está portado"));
    // 52cbf4..52cc04: sólo los BMD de [ad,151) avanzan la animación común.
    constexpr std::int16_t FirstAnimatedModel = 0xad;
    constexpr std::int16_t LastAnimatedModel = 0x151;
    const bool animated = effect.type >= FirstAnimatedModel && effect.type < LastAnimatedModel;
    StepModelEffect(effect, animated ? skeleton : nullptr);
    if (wholeStep)
        effect.previousPosition = before;
    // 0x52cdb3..0x52cdfb: el 0x5ac/0 vivo con la vida impar da otro paso entero, con su propio rand().
    if (effect.alive && effect.type == IcarusFlareEffect && effect.subtype == 0 && (effect.life & 1) != 0)
    {
        auto again = StepGameModelEffect(effect, skeleton, particles, worldClock, random, kalima, services);
        // D-C4: el dibujo interpola desde antes de los dos pasos.
        effect.previousPosition = before;
        return again;
    }
    return {};
}
} // namespace Client::World
