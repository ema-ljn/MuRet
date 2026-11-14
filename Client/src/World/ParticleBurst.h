#pragma once

#include "Core/Expected.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
// 0x51fe10(posición, 0): veinte intentos de 0x5c5/2, sin la explosión opcional del caller de combate.
// Cada intento consume sus dos sorteos antes de buscar slot; un pool lleno no frena la ráfaga.
[[nodiscard]] Core::Expected<void, ParticleError> CreatePushBurst(Particles& particles, const glm::vec3& position,
                                                                  Render::Models::CrtRandom& random);
} // namespace Client::World
