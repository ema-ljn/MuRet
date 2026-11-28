#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::World
{
// 0x51fe10: los veinte intentos de chispa y, con el segundo argumento distinto de cero, la explosión.
// Devuelve si se creó la explosión: sólo en ese caso el caller publica el sonido 0x40 de 0x542a7c.
[[nodiscard]] Core::Expected<bool, ParticleError> CreateParticleBurst(Particles& particles, const glm::vec3& position,
                                                                      Crt::CrtRandom& random, bool explosion,
                                                                      float worldClock = 0.0f);

// 0x51fe10(posición, 0): veinte intentos de 0x5c5/2, sin la explosión opcional del caller de combate.
// Cada intento consume sus dos sorteos antes de buscar slot; un pool lleno no frena la ráfaga.
[[nodiscard]] Core::Expected<void, ParticleError> CreatePushBurst(Particles& particles, const glm::vec3& position,
                                                                  Crt::CrtRandom& random);
} // namespace Client::World
