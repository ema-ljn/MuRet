#include "World/Particles.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::World
{
namespace
{
constexpr std::int32_t BreathSubtype = 0;
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
constexpr double CloudRotationStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
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

// 0x69fd3c (_ftol): fistp de 64 bits truncando y la parte baja en eax. Con un NaN o fuera de rango, la x87
// guarda 0x8000000000000000 y eax queda en 0.
std::int32_t FtolLow(float value)
{
    constexpr float Limit = 9.2233720368547758e18f;
    if (std::isnan(value) || value >= Limit || value < -Limit)
    {
        return 0;
    }
    const auto whole = static_cast<std::int64_t>(value);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole)));
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
} // namespace

std::optional<std::size_t> Particles::Create(std::int32_t type, const glm::vec3& position, const glm::vec3& angles,
                                             const glm::vec3& light, std::int32_t subtype, float scale,
                                             float worldClock, Render::Models::CrtRandom& random)
{
    const bool breath = type == BreathParticle && subtype == BreathSubtype;
    const bool spark = type == SparkParticle && subtype == RavenSparkSubtype;
    const bool cloud = type == CloudParticle && subtype == LoginCloudSubtype;
    if (!breath && !spark && !cloud)
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < m_particles.size(); ++index)
    {
        Particle& particle = m_particles[index];
        if (particle.alive)
        {
            continue;
        }
        // Lo común de 0x53fe9c..0x53ff37. +0x60 no se escribe: conserva el uso anterior del lugar.
        particle = Particle{.alive = true,
                            .type = type,
                            .texture = type,
                            .subtype = subtype,
                            .scale = scale,
                            .position = position,
                            .angles = angles,
                            .light = light,
                            .life = DefaultLife,
                            .owner = 0,
                            .rotation = 0.0f,
                            .field48 = 0.0f,
                            .moves = true,
                            .verticalSpeed = 0.0f,
                            .velocity = glm::vec3(0.0f),
                            .rotationFactor = particle.rotationFactor,
                            .start = position};
        if (cloud)
        {
            CreateTheCloud(particle, random);
            return index;
        }
        if (spark)
        {
            // 0x5406e5: vida 10, los ángulos (0, 0, yaw) y la velocidad, el empujón (−(rand() % 2 + 2), 0, 0)
            // (el idiom de and 0x80000001, fild, fadd 2 de 0x6b8818 y fchs) girado por AngleMatrix de esos
            // ángulos.
            particle.life = SparkLife;
            particle.angles = glm::vec3(0.0f, 0.0f, angles.z);
            const glm::vec3 push(-(static_cast<float>(random.Next() % 2) + SparkPush), 0.0f, 0.0f);
            particle.velocity =
                Render::Models::OriginalVectorRotate(push, Render::Models::OriginalAngleMatrix(particle.angles));
            return index;
        }
        // 0x54184b: el aliento. El rand() % 32 va con el idiom de and 0x8000001f.
        particle.life = BreathLife;
        particle.scale = F32(static_cast<double>((random.Next() % 32) + BreathScaleBase) * BreathScaleStep);
        particle.angles.x = static_cast<float>(random.Next() % FullTurn);
        particle.rotation = static_cast<float>(FtolLow(worldClock) % FullTurn);
        return index;
    }
    return std::nullopt;
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

void StepParticles(Particles& particles, Render::Models::CrtRandom& random)
{
    StepTheWind(particles.Wind(), random);
    for (Particle& particle : particles.All())
    {
        if (!particle.alive)
        {
            continue;
        }
        --particle.life;
        if (particle.life <= 0)
        {
            particle.alive = false;
        }
        if (particle.moves)
        {
            Move(particle);
        }
        if (IsBreath(particle))
        {
            StepTheBreath(particle);
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
}

std::optional<ParticleDraw> ParticleDrawOf(Particle& particle, const glm::vec2& textureSize, int components,
                                           float worldClock, std::size_t slotIndex)
{
    const bool cloud = IsLoginCloud(particle);
    if (!particle.alive || (!IsBreath(particle) && !IsSpark(particle) && !cloud))
    {
        return std::nullopt;
    }
    ParticleDraw::Blend blend = components == 3 ? ParticleDraw::Blend::Additive : ParticleDraw::Blend::Alpha;
    if (cloud)
    {
        blend = ParticleDraw::Blend::Subtractive;
        const double direction = slotIndex % 2 == 0 ? CloudRotationStep : -CloudRotationStep;
        particle.rotation =
            F32(static_cast<double>(worldClock) * static_cast<double>(particle.rotationFactor) * direction +
                static_cast<double>(particle.start.y));
    }
    // 0x5485a3: el ancho y el alto de la textura por la escala, guardados en float.
    return ParticleDraw{
        .texture = particle.texture,
        .blend = blend,
        .position = particle.position,
        .width = F32(static_cast<double>(textureSize.x) * static_cast<double>(particle.scale)),
        .height = F32(static_cast<double>(textureSize.y) * static_cast<double>(particle.scale)),
        .light = particle.light,
        .rotation = particle.rotation,
        .uv = glm::vec2(0.0f),
        .uvSize = glm::vec2(1.0f),
    };
}
} // namespace Client::World
