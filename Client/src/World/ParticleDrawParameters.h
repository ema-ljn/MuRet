#pragma once
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
namespace Client::World
{
struct Particle;
enum class ParticleDrawKind
{
    Common,
    Fire,
    Drop,
    Unrotated
};
struct ParticleDrawParameters
{
    float width;
    float height;
    float rotation;
    glm::vec2 uv;
    glm::vec2 uvSize;
};
// Fórmulas compartidas; extents ya contiene los dos stores F32 previos al servicio.
[[nodiscard]] ParticleDrawParameters ParticleDrawParametersOf(const Particle& particle, glm::vec2 extents,
                                                              ParticleDrawKind kind);
void UpdateParticleCloudDrawRotation(Particle& particle, float clock, std::size_t physicalIndex);
[[nodiscard]] glm::vec3 ScaledParticleDrawLight(const Particle& particle);
} // namespace Client::World
