#pragma once

#include "Core/Expected.h"
#include "World/Particles.h"

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
[[nodiscard]] bool IsNoriaParticle(std::int32_t type, std::int32_t subtype);
void CreateNoriaParticle(Particle& particle, Render::Models::CrtRandom& random);
[[nodiscard]] Core::Expected<void, ParticleError> StepNoriaParticle(Particle& particle,
                                                                    const ParticleTerrainHeight& terrainHeight);
} // namespace Client::World
