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
