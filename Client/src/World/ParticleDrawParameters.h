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
    RotatedFire,
    Drop,
    Unrotated,
    // 0x54899a, la burbuja 698 de Crywolf: la columna de 8 cuadros de coll.jpg.
    Bubble,
    // 0x54864b, la explosión 5ed del terremoto: los cuatro por cuatro cuadros de Explotion01.jpg.
    Explosion
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
