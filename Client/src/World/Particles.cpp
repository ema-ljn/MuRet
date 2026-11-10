#include "World/Particles.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Ftol.h"
#include "World/IParticleTrigonometry.h"
#include "World/NoriaParticleKernels.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace Client::World
{
namespace
{
constexpr std::int32_t BreathSubtype = 0;
// Extensiones 53fe70/5446e0/548550 para 67/0 y 68/0 (los tipos, en Particles.h); recursos y backend
// se validan por separado. La 68 nace como la 67 y después corre la posición.
constexpr std::int32_t Particle67Life = 20;
// Extensión 5446e0 de la nube 622/8: +38 usa estos operandos binarios.
constexpr double Cloud8Rise = std::bit_cast<double>(std::uint64_t{0x3fa47ae147ae147bull});
constexpr float Cloud8RiseLimit = std::bit_cast<float>(0x3f800000u);
constexpr float Cloud8FallLimit = std::bit_cast<float>(0x3dcccccdu);
constexpr float Cloud8Fall = std::bit_cast<float>(0x3ccccccdu);
constexpr double Cloud8RandomStep = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr float Cloud8InitialQ = std::bit_cast<float>(0x3dcccccdu);
// 5446e0_ext_67 y contrato histórico íntegro leaf-4303c0 (predicado raw32 == 34, sin calls).
constexpr std::int32_t Particle67ColorMap = 34;
constexpr double Particle67Growth = static_cast<double>(std::bit_cast<float>(0x3cf5c28fu));
constexpr double Particle67Fade = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr float Particle67Blue = std::bit_cast<float>(0x3f333333u);
constexpr int Particle67ScaleSpan = 6;
constexpr int Particle67ScaleBase = 8;
constexpr double Particle67ScaleStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
// La 68 corre cada eje rand() % 10 − 5, en x, y, z.
constexpr int Particle68OffsetSpan = 10;
constexpr int Particle68OffsetCenter = 5;
// 53fe70/5446e0, extensión 5c6/9 (mapa3, objeto322).
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
// Contratos final-runtime 53fe70/5446e0/548550, casos 5d9/0..3 y 5f2/2.
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
// 0x53ff05: la vida de cualquier partícula al crearla; 0x54184b: la del aliento.
constexpr std::int32_t DefaultLife = 2;
constexpr std::int32_t BreathLife = 16;
// 0x5406eb: la de la chispa del cuervo, y lo que suma a su rand() % 2 (0x6b8818).
constexpr std::int32_t SparkLife = 10;
constexpr float SparkPush = 2.0f;
constexpr double SparkShrink = static_cast<double>(std::bit_cast<float>(0x3F666666u));
constexpr double SparkFade = static_cast<double>(std::bit_cast<float>(0x3F788B30u));
// 0x543407..0x5434d3: neblina del login, cinco rand() y constantes binary32 del PE.
constexpr std::int32_t CloudLife = 500;
constexpr int CloudOffsetSpan = 400;
constexpr int CloudOffsetCenter = 200;
constexpr int CloudScaleSpan = 15;
constexpr int CloudScaleBase = 10;
constexpr int CloudSpeedSpan = 10;
constexpr int CloudSpeedBase = 5;
constexpr double CloudScaleStep = static_cast<double>(std::bit_cast<float>(0x3d888889u));
constexpr double CloudSpeedStep = static_cast<double>(std::bit_cast<float>(0x3df5c28fu));
// 0x5475b6..0x547699 y 0x548c04..0x548c2f; 06 § Los efectos del login, neblina.
constexpr float CloudRotationFactor = 1.5f;
constexpr double CloudGrowth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr float CloudLightLimit = std::bit_cast<float>(0x3e19999au);
constexpr double CloudFade = static_cast<double>(std::bit_cast<float>(0x3f73cf3eu));
constexpr double CloudGrowthRadius = 300.0;
constexpr double CloudDeathRadius = 500.0;
constexpr double BreathScaleStep = static_cast<double>(0.01f); // 0x6b88d4
constexpr int BreathScaleBase = 48;
constexpr int FullTurn = 360;
// 0x545ec4: la luz es la vida por 0,125 (0x6ba170), la velocidad vertical suma 0,2 (0x6b8854) y la escala,
// 0,05 (0x6b8a48).
constexpr double BreathLightPerLife = 0.125;
constexpr double BreathRise = static_cast<double>(0.2f);
constexpr double BreathGrowth = static_cast<double>(0.05f);
// 0x5446e0: el cambio del viento, (rand() % 2001 − 1000) · 0,0006 (0x6b8a50), entre ±0,6 (0x6b8a20 y
// 0x6b8b8c), y el viento, entre ±1,7 (0x6ba3f0 y 0x6ba3ec).
constexpr int WindSpan = 2001;
constexpr int WindCenter = 1000;
constexpr double WindStep = static_cast<double>(0.0006f);
constexpr float ChangeLimit = 0.6f;
constexpr float WindLimit = 1.7f;

float F32(double value)
{
    return static_cast<float>(value);
}

// El tope de 0x544730..0x544773 (y de 0x54479a..0x5447de), con las comparaciones de la x87: si no es
// mayor o igual que el de arriba (C0, o sin orden), y el de abajo es mayor, el de abajo; si después
// sigue sin ser mayor o igual que el de arriba, el valor; si no, el de arriba. Un NaN pasa.
float ClampLikeTheBinary(float value, float low, float high)
{
    const bool belowHigh = value < high || std::isnan(value);
    if (belowHigh && !(low < value || low == value || std::isnan(value)))
    {
        return low;
    }
    return belowHigh ? value : high;
}

// 0x4e98d0: la posición más la velocidad girada por AngleMatrix de los ángulos, cada suma en 53 bits.
void Move(Particle& particle)
{
    const glm::vec3 turned =
        Render::Models::OriginalVectorRotate(particle.velocity, Render::Models::OriginalAngleMatrix(particle.angles));
    for (int k = 0; k < 3; ++k)
    {
        particle.position[k] = F32(static_cast<double>(turned[k]) + static_cast<double>(particle.position[k]));
    }
}

void StepTheWind(ParticleWind& wind, Render::Models::CrtRandom& random)
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

// 0x545ec4: el aliento sube, crece y se apaga con la vida. La z suma la velocidad vertical antes de que
// se guarde en float (fst la deja en la pila).
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

bool IsSpark(const Particle& particle)
{
    return particle.type == SparkParticle && particle.subtype == RavenSparkSubtype;
}

bool IsLoginCloud(const Particle& particle)
{
    return particle.type == CloudParticle && particle.subtype == LoginCloudSubtype;
}

bool IsCloud8(const Particle& particle)
{
    return particle.type == CloudParticle && particle.subtype == 8;
}

void CreateTheCloud8(Particle& particle, float inputScale, Render::Models::CrtRandom& random)
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

    // El cuerpo vuelve a poner los tres dwords a cero tras R7.
    particle.velocity = glm::vec3(0.0f);
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
        // FCOMP toma también unordered por la suma, equivalente a q<1 o unordered.
        if (!(q >= Cloud8RiseLimit))
        {
            particle.field38 = std::bit_cast<std::uint32_t>(F32(static_cast<double>(q) + Cloud8Rise));
        }
        particle.position.z = F32(static_cast<double>(particle.position.z) + 1.0);
        return;
    }

    // La comparación tardía exige estrictamente q>0.1f; unordered conserva +38.
    if (q > Cloud8FallLimit)
    {
        particle.field38 = std::bit_cast<std::uint32_t>(F32(static_cast<double>(q) - Cloud8Fall));
    }
    particle.position.z = F32(static_cast<double>(particle.position.z) - 0.5);
}

bool IsFire(const Particle& particle)
{
    return particle.type == FireParticle && ((particle.subtype >= 0 && particle.subtype <= 3) || particle.subtype == 8);
}

bool IsWorldSmoke(const Particle& particle)
{
    return particle.type == BreathParticle && particle.subtype == WorldSmokeSubtype;
}

bool IsMapSpark(const Particle& particle)
{
    return particle.type == SparkParticle && particle.subtype == MapSparkSubtype;
}

bool IsDrop(const Particle& particle)
{
    return particle.type == DropParticle && particle.subtype == 0;
}

std::optional<ParticleError> BindingError(std::int32_t type, std::int32_t subtype)
{
    if (type == ShinyParticle || type == FairySparkParticle)
        return IsNoriaParticle(type, subtype) ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    if (type == Particle67 || type == Particle68)
    {
        return subtype == 0 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == DropParticle)
    {
        return subtype == 0 ? std::nullopt : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == FireParticle)
    {
        return ((subtype >= 0 && subtype <= 3) || subtype == 8) ? std::nullopt
                                                                : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == BreathParticle)
    {
        return subtype == BreathSubtype || subtype == WorldSmokeSubtype
                   ? std::nullopt
                   : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == SparkParticle)
    {
        return subtype == TeleportSparkSubtype || subtype == RavenSparkSubtype || subtype == MapSparkSubtype
                   ? std::nullopt
                   : std::optional(ParticleError::UnsupportedSubtype);
    }
    if (type == CloudParticle)
    {
        return subtype == LoginCloudSubtype || subtype == 8 ? std::nullopt
                                                            : std::optional(ParticleError::UnsupportedSubtype);
    }
    return ParticleError::UnsupportedType;
}

// 53fe9c..53ff37: copias intercaladas y relectura de P, sin snapshot ni reset del slot reutilizado.
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
    particle.moves = true;
    for (int axis = 0; axis < 3; ++axis)
    {
        particle.angles[axis] = angles[axis];
    }
    particle.velocity = glm::vec3(0.0f);
}

// 0x54059f..0x54061c: tres ángulos de 0..359 y VectorRotate(0,-50,0); conserva la vida común (2).
void CreateTheTeleportSpark(Particle& particle, Render::Models::CrtRandom& random)
{
    constexpr int FullCircle = 360;
    constexpr float Speed = -50.0f;
    for (int axis = 0; axis < 3; ++axis)
        particle.angles[axis] = static_cast<float>(random.Next() % FullCircle);
    particle.velocity = Render::Models::OriginalVectorRotate(glm::vec3(0.0f, Speed, 0.0f),
                                                             Render::Models::OriginalAngleMatrix(particle.angles));
}

void CreateTheFire(Particle& particle, Render::Models::CrtRandom& random)
{
    particle.life = FireLife;
    particle.velocity.x = 0.0f;
    const int speed = (random.Next() % FireSpeedSpan) + FireSpeedBase;
    particle.velocity.z = 0.0f;
    const std::int32_t subtype = particle.subtype; // Relectura posterior a R1 (00541197).
    particle.velocity.y = F32(static_cast<double>(speed) * FireSpeed);
    if (subtype == 0)
    {
        particle.scale = F32(static_cast<double>((random.Next() % FireScaleSpan) + FireScaleBase) * BreathScaleStep);
    }
    if (particle.subtype == 1) // 005413c2 vuelve a leer el subtipo vivo.
    {
        particle.scale =
            F32(static_cast<double>((random.Next() % FireSmallScaleSpan) + FireSmallScaleBase) * BreathScaleStep);
    }
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
}

void CreateTheWorldSmoke(Particle& particle, Render::Models::CrtRandom& random)
{
    particle.life = WorldSmokeLife;
    particle.scale = F32(static_cast<double>((random.Next() % FireScaleSpan) + SmokeScaleBase) * BreathScaleStep);
    particle.rotation = static_cast<float>(random.Next() % FullTurn);
    particle.verticalSpeed = F32(static_cast<double>((random.Next() % FireSpeedBase) + SmokeSpeedBase) * SmokeSlowdown);
}

// Contrato53fe70-map-variants §5f2/6..8 y Step5446e0 §5f2/4,6,7,8.
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

// Contrato53fe70-map-variants §5f2/6..8: cada R precede sus relecturas propias.
bool IsMap8Smoke(std::int32_t type, std::int32_t subtype)
{
    return type == BreathParticle && (subtype == 4 || subtype == 6 || subtype == 7 || subtype == 8);
}

void CreateTheMap8Smoke(Particle& p, float argumentScale, Render::Models::CrtRandom& random)
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

// Contrato5446e0 §5f2/4: Z usa el acumulador ancho, no su store f32.
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

// Contrato5446e0 §5f2/6 y trig-design §Step5f2/6: el evaluador devuelve
// el producto53 de una salida retenida64. t sigue independiente hasta FTOL.
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
    const std::int32_t low = Client::World::FtolLow(t);
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

void CreateTheParticle67(Particle& particle, Render::Models::CrtRandom& random)
{
    particle.life = Particle67Life;
    const int scaleSteps = (random.Next() % Particle67ScaleSpan) + Particle67ScaleBase;
    particle.scale = F32(static_cast<double>(scaleSteps) * Particle67ScaleStep);
}

// La vida, el rand() y la escala de la 67, y después un rand() por eje para la posición.
void CreateTheParticle68(Particle& particle, Render::Models::CrtRandom& random)
{
    CreateTheParticle67(particle, random);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int offset = (random.Next() % Particle68OffsetSpan) - Particle68OffsetCenter;
        particle.position[axis] = F32(static_cast<double>(offset) + static_cast<double>(particle.position[axis]));
    }
}

// El paso de la 67 y de la 68, que es el mismo.
std::optional<ParticleError> StepTheParticle67Or68(Particle& particle, const std::int32_t* map)
{
    if (particle.life <= 0)
    {
        particle.alive = false;
    }
    particle.scale = particle.subtype == 1
                         ? F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.scale))
                         : F32(static_cast<double>(particle.scale) + Particle67Growth);
    // 4303c0 (contrato leaf-4303c0) relee aquí el DWORD raw32 del mapa, no el byte bajo ni un mapa
    // capturado al entrar, y devuelve AL=1 sólo para 34.
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

void CreateTheMapSpark(Particle& particle, Render::Models::CrtRandom& random)
{
    particle.life = MapSparkLife;
    particle.angles.x = 0.0f;
    const int yaw = random.Next() % FullTurn;
    particle.angles.z = 0.0f;
    particle.angles.y = static_cast<float>(yaw);
    const auto matrix = Render::Models::OriginalAngleMatrix(particle.angles);
    glm::vec3 offset(0.0f);
    offset.z = F32(static_cast<double>(random.Next() % MapSparkOffsetSpan) + MapSparkOffsetBase);
    particle.velocity = Render::Models::OriginalVectorRotate(offset, matrix);
    // 005408c8/d7/e8 sustituyen velocidad después de cargar cada suma, antes de guardar posición.
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
    // TEST AH,1 comprueba C0: menor y unordered apagan, usando ST0 anterior al store32.
    if (scale < MapSparkMinimum || std::isnan(scale))
    {
        particle.alive = false;
    }
}

void CreateTheDrop(Particle& particle, Render::Models::CrtRandom& random)
{
    particle.life = DropLifeBase + (random.Next() % DropLifeSpan);
    const int scaleSteps = (random.Next() % DropScaleSpan) + DropScaleBase;
    particle.light = glm::vec3(1.0f);
    particle.scale = F32(static_cast<double>(scaleSteps) * DropScaleStep);
}

void StepTheDrop(Particle& particle, Render::Models::CrtRandom& random)
{
    const std::uint32_t frame = std::bit_cast<std::uint32_t>(particle.field48) + 1u;
    particle.field48 = std::bit_cast<float>(frame);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int direction = axis == 2 ? DropWalkCenter : -DropWalkCenter;
        const int steps = (random.Next() % DropWalkSpan) + direction;
        // La escala se relee después de cada RNG; no hay store32 entre los dos productos.
        particle.position[axis] =
            F32(static_cast<double>(steps) * static_cast<double>(particle.scale) * DropWalkFactor +
                static_cast<double>(particle.position[axis]));
    }
}

std::int32_t WrapSubtract(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

void StepTheFire(Particle& particle)
{
    particle.verticalSpeed = F32(static_cast<double>(particle.verticalSpeed) + FireRise);
    if (particle.subtype == 0)
    {
        particle.scale = F32(static_cast<double>(particle.scale) - FireShrink);
    }
    else if (particle.subtype == 8)
    {
        // 548270[8]→54585f: no amortigua velocidad ni agrega verticalSpeed a escala.
        particle.scale = F32(static_cast<double>(particle.scale) * .95f);
        particle.rotation = F32(static_cast<double>(particle.rotation) + 5);
    }
    else
    {
        particle.scale = F32(static_cast<double>(particle.verticalSpeed) + static_cast<double>(particle.scale));
        for (int axis = 0; axis < 3; ++axis)
        {
            particle.velocity[axis] = F32(static_cast<double>(particle.velocity[axis]) * FireDamping);
        }
    }
    const std::int32_t frame = WrapSubtract(FireLife - 1, particle.life) / FrameSteps;
    particle.position.z =
        F32(static_cast<double>(particle.verticalSpeed) * FireHeightFactor + static_cast<double>(particle.position.z));
    particle.field48 = std::bit_cast<float>(frame);
}

void StepTheWorldSmoke(Particle& particle)
{
    particle.light = glm::vec3(F32(static_cast<double>(particle.life) * SmokeFade));
    // FST +50 no redondea ST0: X y Z usan el acumulador double retenido en este paso.
    const double speed = static_cast<double>(particle.verticalSpeed) - SmokeSlowdown;
    particle.verticalSpeed = F32(speed);
    particle.position.x = F32(static_cast<double>(particle.position.x) - speed * SmokeDrift);
    particle.position.z = F32(speed + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) - BreathScaleStep);
}

void CreateTheCloud(Particle& particle, Render::Models::CrtRandom& random)
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
    // 0x547639: compara antes de recargar el rojo almacenado; un NaN no alcanza el umbral.
    if (red >= static_cast<double>(CloudLightLimit))
    {
        particle.light = glm::vec3(CloudLightLimit);
    }
}

void StepTheCloud(Particle& particle)
{
    particle.rotationFactor = CloudRotationFactor;
    // 0x5475be: X queda sin recargar después del fst; Y y Z se usan ya guardados en float.
    const double x = static_cast<double>(particle.velocity.x) + static_cast<double>(particle.position.x);
    particle.position.x = F32(x);
    particle.position.y = F32(static_cast<double>(particle.velocity.y) + static_cast<double>(particle.position.y));
    particle.position.z = F32(static_cast<double>(particle.velocity.z) + static_cast<double>(particle.position.z));
    const double dx = x - static_cast<double>(particle.start.x);
    const double dy = static_cast<double>(particle.position.y) - static_cast<double>(particle.start.y);
    const double dz = static_cast<double>(particle.position.z) - static_cast<double>(particle.start.z);
    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
    // 0x547602: unordered toma la misma rama que <= 300 por los flags de la x87.
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

// 0x545209 → 0x545f9e: la chispa se achica por 0,9 (0x6b8a1c) y se apaga por 1/1,03 (0x6ba3b0), cada
// producto guardado en float.
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

// Create53fe70 §60f/3: cuatroR, relecturas después deR4 y continuador tardío10.
Core::Expected<void, ParticleError> CreateTheOrbit(Particle& particle, float inputScale,
                                                   Render::Models::CrtRandom& random,
                                                   IParticleTrigonometry& trigonometry)
{
    particle.life = OrbitLife;
    const int rise = random.Next() % OrbitSpeedSpan;
    const double riseUnit = static_cast<double>(rise) * static_cast<double>(OrbitUnit);
    const double riseRange = riseUnit * static_cast<double>(OrbitRiseRange);
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

// Step5446e0 §60f/3: leídos los selectores tras el helper; sin cargas de reloj nuevas.
Core::Expected<void, ParticleError> StepTheOrbit(Particle& particle, IParticleTrigonometry& trigonometry)
{
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
    particle.position.z = F32(rise + static_cast<double>(particle.position.z));
    particle.scale = F32(static_cast<double>(particle.scale) - static_cast<double>(OrbitShrink));
    if (particle.life <= OrbitFadeLife)
    {
        particle.light.x = F32(static_cast<double>(particle.light.x) * static_cast<double>(OrbitFade));
        particle.light.y = F32(static_cast<double>(particle.light.y) * static_cast<double>(OrbitFade));
        particle.light.z = F32(static_cast<double>(particle.light.z) * static_cast<double>(OrbitFade));
    }
    return {};
}
} // namespace

std::optional<std::size_t> Particles::Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                             const glm::vec3& light, std::int32_t subtype, float scale,
                                             float worldClock, Render::Models::CrtRandom& random)
{
    auto created = CreateChecked(type, position, angles, light, subtype, scale, worldClock, random);
    return created.HasValue() ? created.Value() : std::nullopt;
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                         std::int32_t subtype, float scale, float worldClock, Render::Models::CrtRandom& random,
                         std::uint32_t owner)
{
    return CreateImpl(type, position, angles, light, subtype, scale, worldClock, nullptr, random, owner, nullptr);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateChecked(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                         std::int32_t subtype, float scale, std::reference_wrapper<const float> worldClock,
                         Render::Models::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry& trigonometry)
{
    return CreateImpl(type, position, angles, light, subtype, scale, 0.0f, &worldClock.get(), random, owner,
                      &trigonometry);
}

Core::Expected<std::optional<std::size_t>, ParticleError>
Particles::CreateImpl(std::int32_t type, const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                      std::int32_t subtype, float scale, float legacyClock, const float* worldClock,
                      Render::Models::CrtRandom& random, std::uint32_t owner, IParticleTrigonometry* trigonometry)
{
    for (std::size_t index = 0; index < m_particles.size(); ++index)
    {
        Particle& particle = m_particles[index];
        if (particle.alive)
        {
            continue;
        }
        const bool orbit = trigonometry != nullptr && type == OrbitParticle;
        if (orbit && subtype != OrbitSubtype)
        {
            return Core::Unexpected(ParticleError::UnsupportedSubtype);
        }
        if (!orbit && !(worldClock != nullptr && IsMap8Smoke(type, subtype)))
        {
            if (const auto error = BindingError(type, subtype))
            {
                return Core::Unexpected(*error);
            }
        }
        Initialize(particle, type, position, angles, light, subtype, scale, owner);
        if (orbit)
        {
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
            CreateNoriaParticle(particle, random);
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
        if (type == FireParticle)
        {
            CreateTheFire(particle, random);
            return std::optional(index);
        }
        if (type == BreathParticle && subtype == WorldSmokeSubtype)
        {
            CreateTheWorldSmoke(particle, random);
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
        if (type == SparkParticle)
        {
            // 0x5406e5: vida 10, los ángulos (0, 0, yaw) y la velocidad, el empujón (−(rand() % 2 + 2), 0, 0)
            // (el idiom de and 0x80000001, fild, fadd 2 de 0x6b8818 y fchs) girado por AngleMatrix de esos
            // ángulos.
            particle.life = SparkLife;
            particle.angles = glm::vec3(0.0f, 0.0f, angles.z);
            const glm::vec3 push(-(static_cast<float>(random.Next() % 2) + SparkPush), 0.0f, 0.0f);
            particle.velocity =
                Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(particle.angles));
            return std::optional(index);
        }
        // 0x54184b: el aliento. El rand() % 32 va con el idiom de and 0x8000001f.
        particle.life = BreathLife;
        particle.scale = F32(static_cast<double>((random.Next() % 32) + BreathScaleBase) * BreathScaleStep);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(
            Client::World::FtolLow(static_cast<double>(worldClock != nullptr ? *worldClock : legacyClock)) % FullTurn);
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
Core::Expected<void, ParticleError> StepParticlesImpl(Particles& particles, Render::Models::CrtRandom& random,
                                                      const float* worldClock, const std::int32_t* map,
                                                      IParticleTrigonometry* trigonometry,
                                                      const ParticleTerrainHeight& terrainHeight)
{
    // Los punteros privados distinguen exclusivamente legacy de la API pública requerida.
    // El overload vivo siempre proporciona reloj y evaluador; no hay servicio opcional público.
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
        if (trigonometry != nullptr && particle.type == OrbitParticle)
        {
            if (particle.subtype != OrbitSubtype)
            {
                return Core::Unexpected(ParticleError::UnsupportedSubtype);
            }
            const auto stepped = StepTheOrbit(particle, *trigonometry);
            if (!stepped.HasValue())
            {
                return stepped;
            }
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
        if (particle.type != Particle67 && particle.type != Particle68 && !IsCloud8(particle))
        {
            if (const auto error = BindingError(particle.type, particle.subtype))
            {
                return Core::Unexpected(*error);
            }
        }
        if (IsNoriaParticle(particle.type, particle.subtype))
        {
            const auto stepped = StepNoriaParticle(particle, terrainHeight);
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
        else if (IsDrop(particle))
        {
            StepTheDrop(particle, random);
        }
        else if (IsFire(particle))
        {
            StepTheFire(particle);
        }
        else if (IsWorldSmoke(particle))
        {
            StepTheWorldSmoke(particle);
        }
        else if (IsBreath(particle))
        {
            StepTheBreath(particle);
        }
        else if (IsMapSpark(particle))
        {
            StepTheMapSpark(particle);
        }
        else if (particle.type == SparkParticle && particle.subtype == TeleportSparkSubtype)
        {
            // 0x5450a9: resta 2 y muere bajo 0,2 (también unordered); no cambia la luz.
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
        else if (IsLoginCloud(particle))
        {
            StepTheCloud(particle);
        }
    }
    return {};
}

} // namespace

Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles, Render::Models::CrtRandom& random,
                                                         const std::int32_t* map,
                                                         const ParticleTerrainHeight& terrainHeight)
{
    return StepParticlesImpl(particles, random, nullptr, map, nullptr, terrainHeight);
}

Core::Expected<void, ParticleError> StepParticlesChecked(Particles& particles, Render::Models::CrtRandom& random,
                                                         std::reference_wrapper<const float> worldClock,
                                                         const std::int32_t* map, IParticleTrigonometry& trigonometry,
                                                         const ParticleTerrainHeight& terrainHeight)
{
    return StepParticlesImpl(particles, random, &worldClock.get(), map, &trigonometry, terrainHeight);
}

std::optional<ParticleError> StepParticles(Particles& particles, Render::Models::CrtRandom& random)
{
    const auto stepped = StepParticlesChecked(particles, random);
    return stepped.HasValue() ? std::nullopt : std::optional(stepped.Error());
}
} // namespace Client::World
