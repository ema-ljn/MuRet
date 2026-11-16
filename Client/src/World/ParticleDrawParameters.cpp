#include "World/ParticleDrawParameters.h"
#include "World/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <bit>
#include <cstddef>
#include <cstdint>
namespace Client::World
{
namespace
{
constexpr double Quarter = 0.25;
constexpr std::int32_t AtlasColumns = 4;
constexpr std::int32_t DropFrames = 9;
constexpr std::int32_t DropColumns = 3;
constexpr double CloudRotationStep = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));
constexpr double DropInset = static_cast<double>(std::bit_cast<float>(0x3ba3d70au));
constexpr float DropSize = std::bit_cast<float>(0x3e75c28fu);
} // namespace
ParticleDrawParameters ParticleDrawParametersOf(const Particle& particle, glm::vec2 extents, ParticleDrawKind kind)
{
    ParticleDrawParameters result{extents.x, extents.y, 0, {0, 0}, {1, 1}};
    if (kind == ParticleDrawKind::Common)
        result.rotation = particle.rotation;
    if (kind == ParticleDrawKind::Fire || kind == ParticleDrawKind::RotatedFire)
    {
        result.width = static_cast<float>(static_cast<double>(result.width) * Quarter);
        result.rotation = kind == ParticleDrawKind::RotatedFire ? particle.rotation : particle.angles.x;
        const std::int32_t frame = std::bit_cast<std::int32_t>(particle.field48);
        result.uv.x = static_cast<float>(static_cast<double>(frame % AtlasColumns) * Quarter);
        result.uvSize.x = static_cast<float>(Quarter);
    }
    if (kind == ParticleDrawKind::Bubble)
    {
        // 0x54899a..0x5489e0: el alto por el double 0,125 (0x6ba3f8), el giro de +0x20 y la v del cuadro (+0x48 % 8,
        // con el and de la corrección del signo) por 0,125f (0x6ba170); la u en 0 y el tamaño (1; 0,125).
        constexpr std::int32_t BubbleFrames = 8;
        constexpr double BubbleRow = 0.125;
        result.height = static_cast<float>(static_cast<double>(result.height) * BubbleRow);
        result.rotation = particle.angles.x;
        const std::int32_t frame = std::bit_cast<std::int32_t>(particle.field48) % BubbleFrames;
        result.uv.y = static_cast<float>(static_cast<double>(frame) * BubbleRow);
        result.uvSize = glm::vec2(1.0f, static_cast<float>(BubbleRow));
    }
    if (kind == ParticleDrawKind::Explosion)
    {
        // 0x54864b..0x5486b1: sin giro, la u del resto con signo cuadro % 4 (and 0x80000003 con la corrección) y la v
        // del cociente cuadro / 4 (cdq, and 3, sar 2: hacia 0), cada una · 0,25f + 0,005f (0x6b89e0 y 0x6b89d8) en la
        // x87, y el tamaño (0,24f; 0,24f).
        const std::int32_t frame = std::bit_cast<std::int32_t>(particle.field48);
        result.uv.x = static_cast<float>(static_cast<double>(frame % AtlasColumns) * Quarter + DropInset);
        result.uv.y = static_cast<float>(static_cast<double>(frame / AtlasColumns) * Quarter + DropInset);
        result.uvSize = glm::vec2(DropSize);
    }
    if (kind == ParticleDrawKind::Blood)
    {
        // 0x5487b9..0x5487fb: sin giro, la u del resto con signo cuadro % 2 (and 0x80000001 con la corrección) y la v
        // del cociente cuadro / 2 (cdq, sub y sar: hacia 0), cada una · 0,5f (0x6b882c) en la x87, y el tamaño
        // (0,5f; 0,5f).
        constexpr std::int32_t BloodColumns = 2;
        constexpr double Half = 0.5;
        const std::int32_t frame = std::bit_cast<std::int32_t>(particle.field48);
        result.uv.x = static_cast<float>(static_cast<double>(frame % BloodColumns) * Half);
        result.uv.y = static_cast<float>(static_cast<double>(frame / BloodColumns) * Half);
        result.uvSize = glm::vec2(static_cast<float>(Half));
    }
    if (kind == ParticleDrawKind::Drop)
    {
        const std::int32_t frame = std::bit_cast<std::int32_t>(particle.field48) % DropFrames;
        result.uv.y = static_cast<float>(static_cast<double>(frame / DropColumns) * Quarter + DropInset);
        result.uv.x = static_cast<float>(static_cast<double>(frame % DropColumns) * Quarter + DropInset);
        result.uvSize = glm::vec2(DropSize);
    }
    return result;
}
void UpdateParticleCloudDrawRotation(Particle& particle, float clock, std::size_t physicalIndex)
{
    // 548c13..2f: dos productos retenidos y un único store; G se recibe en esta frontera.
    const double direction = physicalIndex % 2 == 0 ? CloudRotationStep : -CloudRotationStep;
    const double global = static_cast<double>(clock) * static_cast<double>(particle.rotationFactor);
    const double signedRotation = global * direction;
    particle.rotation = static_cast<float>(signedRotation + static_cast<double>(particle.start.y));
}
glm::vec3 ScaledParticleDrawLight(const Particle& particle)
{
    glm::vec3 light{};
    for (int axis = 0; axis < 3; ++axis)
    {
        // 548c43..70: cada canal vuelve a leer +38; no altera RGB canónico.
        const float q = std::bit_cast<float>(particle.field38);
        light[axis] = static_cast<float>(static_cast<double>(particle.light[axis]) * static_cast<double>(q));
    }
    return light;
}
} // namespace Client::World
