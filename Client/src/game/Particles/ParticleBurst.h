#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Particles
{
// 0x51fe10: the twenty spark attempts and, with the second argument nonzero, the explosion. Returns whether the
// explosion was created: only then does the caller publish sound 0x40 of 0x542a7c.
[[nodiscard]] Core::Expected<bool, ParticleError> CreateParticleBurst(Particles& particles, const glm::vec3& position,
                                                                      Crt::CrtRandom& random, bool explosion,
                                                                      float worldClock = 0.0f);

// 0x51fe10(position, 0): twenty attempts of 0x5c5/2, without the combat caller's optional explosion. Each attempt takes
// its two rolls before looking for a slot; a full pool does not stop the burst.
[[nodiscard]] Core::Expected<void, ParticleError> CreatePushBurst(Particles& particles, const glm::vec3& position,
                                                                  Crt::CrtRandom& random);
} // namespace Client::Particles
