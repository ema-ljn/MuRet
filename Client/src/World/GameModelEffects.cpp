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
// 0x5208c5..0x52091a: la luz local del paso, del primer rand(): ((r & 3) + 7) · 0,1f (0x6b87b8); con
// menos de 5 de vida, menos (5 − vida) · 0,2f (0x6b8854), guardada en float y comparada sin redondear con 0.
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

// 0x5239c8..0x523b3a con el subtipo 0: la velocidad sin girar sumada y por 0,9f (0x6b8a1c), la caída +0xe4
// que pierde 3; en el suelo se apoya, rebota a −0,5, pierde 4 de vida y se inclina escala · 128 (si no, ·
// 32); con rand() % 10 en 0, un fuego 5d9 con el subtipo rand() % 3 + 1. El 5 sigue a su dueño (y salta la
// cola) y el resto no está portado.
Core::Expected<void, std::string> StepStoneChip(ModelEffect& effect, Particles& particles, float worldClock,
                                                Render::Models::CrtRandom& random,
                                                const ModelEffectStepServices& services)
{
    if (effect.subtype != 0)
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
    if (random.Next() % FireChance != 0)
        return {};
    const int subtype = random.Next() % FireSubtypes + 1;
    const auto created = particles.CreateChecked(FireParticle, position, effect.transform.angles, effect.light, subtype,
                                                 1.0f, worldClock, random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x523b3a: no se pudo crear el fuego 5d9/{} de la piedrita", subtype));
    return {};
}

// 0x52ad21..0x52ae25 con el subtipo 0: seis llamas 5de/0 a ±25 en x e y con la luz local en blanco (los dos
// sorteos de cada una antes de su creación); con rand() & 7 en 0, la piedrita 0xc8 + (rand() & 1) con su
// luz; y la luz dinámica (L; L · 0,4f; 0) de radio 3. La rama del dueño héroe (0x52ae2a) no corre: los que
// crean los objetos del 4 no tienen dueño.
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
    return AddDynamicLight(services.lighting, position, light, LightRadius, "0x52ae25");
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

// 0x52c5ca..0x52c7a6, la onda 0x6eb con el subtipo 1 (la del galope en Icarus): la escala crece (rand() % 5) ·
// 0,1f, la posición tiembla (rand() & 7) − 4 en x y en y, cada una en la x87 hasta su fstp; una escala negativa
// vuelve a 0 (0x52c74e) y, con el subtipo de 0 a 3, la luz es la vida, o 40 menos la vida pasados los 20, por 0,05f
// (0x6b8ad0) en los tres canales. Tres rand(). Los demás subtipos cortan en su rama.
Core::Expected<void, std::string> StepGallopShockWave(ModelEffect& effect, Render::Models::CrtRandom& random)
{
    constexpr int GrowthSpan = 5;
    constexpr double GrowthStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu)); // 0x6b8a34: 0,1f.
    constexpr int ShakeSpan = 8;
    constexpr double ShakeCentre = static_cast<double>(std::bit_cast<float>(0x40800000u)); // 0x6b8a44: 4.
    constexpr std::int32_t FadeTurn = 20, FadeEnd = 40;
    constexpr double LightPerLife = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu)); // 0x6b8ad0: 0,05f.
    if (effect.subtype != GallopShockWaveSubtype)
        return Core::Unexpected(
            std::format("0x52c5ca: el paso del 0x6eb con el subtipo {} no está portado", effect.subtype));
    const int growth = random.Next() % GrowthSpan;
    effect.transform.scale =
        F32((static_cast<double>(growth) * GrowthStep) + static_cast<double>(effect.transform.scale));
    for (int axis = 0; axis < 2; ++axis)
    {
        const int shake = random.Next() % ShakeSpan;
        effect.transform.position[axis] =
            F32((static_cast<double>(shake) - ShakeCentre) + static_cast<double>(effect.transform.position[axis]));
    }
    // fcomp con 0 y C0: también sin orden.
    if (!(effect.transform.scale >= 0.0f))
        effect.transform.scale = 0.0f;
    const std::int32_t fade = effect.life > FadeTurn ? FadeEnd - effect.life : effect.life;
    effect.light = glm::vec3(F32(static_cast<double>(fade) * LightPerLife));
    return {};
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
        if (auto stepped = StepGallopShockWave(effect, random); !stepped.HasValue())
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
