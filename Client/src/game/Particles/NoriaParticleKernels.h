#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class FrameSprites;

[[nodiscard]] bool IsNoriaParticle(std::int32_t type, std::int32_t subtype);
// The 5fc/2, the flash the 0xbd shadow rain drops (0x5254f9): its step leaves a sprite in each one.
[[nodiscard]] bool IsFallingShine(std::int32_t type, std::int32_t subtype);
// The 5c5/6 (0x529f71) follows its owner (+0x40) in the step, without reading it at birth.
[[nodiscard]] bool IsOwnedFairySpark(std::int32_t type, std::int32_t subtype);
// The 5c5/5 (that of the 0x140's step, 0x52a16e) reads its owner's position (+0x40 → +0x10) at birth (0x5403ad) for
// its start and follows it in the step like the /6.
[[nodiscard]] bool IsOwnerBirthFairySpark(std::int32_t type, std::int32_t subtype);
// After the prefix of 0x53fe70; `scale` is the requested one (the /5 and the /6 put it back in +0x10).
// `ownerPosition` is the 5c5/5's owner's: without it, MissingOwner with the first rand() taken (Particles asks for it
// before touching the slot).
[[nodiscard]] Core::Expected<void, ParticleError>
CreateNoriaParticle(Particle& particle, float scale, Crt::CrtRandom& random, const glm::vec3* ownerPosition = nullptr);
// The 5c5/5 and the /6 ask for their owner's position (`ownerStates`): without it, MissingOwner after moving.
[[nodiscard]] Core::Expected<void, ParticleError> StepNoriaParticle(Particle& particle,
                                                                    const ParticleTerrainHeight& terrainHeight,
                                                                    const ParticleOwnerStates& ownerStates);
// 0x546c0f..0x546c9e: without the frame's sprite table (`sprites`), MissingSprites after turning, taking its rand(),
// moving and fading, before the sprite.
[[nodiscard]] Core::Expected<void, ParticleError> StepTheFallingShine(Particle& particle, Crt::CrtRandom& random,
                                                                      FrameSprites* sprites);
} // namespace Client::Particles
