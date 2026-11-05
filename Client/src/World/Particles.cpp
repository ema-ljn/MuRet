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
    if (!breath && !spark)
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
        // Lo común de 0x53fe9c..0x53ff37.
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
                            .start = position};
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
    }
}

std::optional<ParticleDraw> ParticleDrawOf(const Particle& particle, const glm::vec2& textureSize, int components)
{
    // El aliento (0x548707) y la chispa (el camino de siempre, 0x54889a) piden lo mismo.
    if (!IsBreath(particle) && !IsSpark(particle))
    {
        return std::nullopt;
    }
    // 0x5485a3: el ancho y el alto de la textura por la escala, guardados en float.
    return ParticleDraw{
        .texture = particle.texture,
        .additive = components == 3,
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
