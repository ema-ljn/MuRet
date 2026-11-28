#pragma once
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
namespace Client::Particles
{
struct Particle;
enum class ParticleDrawKind
{
    Common,
    Fire,
    RotatedFire,
    Drop,
    Unrotated,
    // 0x54899a, Crywolf's bubble 698: the 8-frame column of coll.jpg.
    Bubble,
    // 0x54864b, the earthquake's explosion 5ed: the four by four frames of Explotion01.jpg.
    Explosion,
    // 0x5487b9, the hit's blood 5e4: the two by two frames of blood.tga, no turn.
    Blood
};
struct ParticleDrawParameters
{
    float width;
    float height;
    float rotation;
    glm::vec2 uv;
    glm::vec2 uvSize;
};
// Shared formulas; extents already holds the two F32 stores before the service.
[[nodiscard]] ParticleDrawParameters ParticleDrawParametersOf(const Particle& particle, glm::vec2 extents,
                                                              ParticleDrawKind kind);
void UpdateParticleCloudDrawRotation(Particle& particle, float clock, std::size_t physicalIndex);
[[nodiscard]] glm::vec3 ScaledParticleDrawLight(const Particle& particle);
} // namespace Client::Particles
