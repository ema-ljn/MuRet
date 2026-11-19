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
class FrameSprites;

[[nodiscard]] bool IsNoriaParticle(std::int32_t type, std::int32_t subtype);
// La 5fc/2, el destello que suelta la lluvia de sombras 0xbd (0x5254f9): su paso deja un sprite en cada uno.
[[nodiscard]] bool IsFallingShine(std::int32_t type, std::int32_t subtype);
// La 5c5/6 (0x529f71) sigue a su dueño (+0x40) en el paso, sin leerlo al nacer.
[[nodiscard]] bool IsOwnedFairySpark(std::int32_t type, std::int32_t subtype);
// Después del prefijo de 0x53fe70; `scale` es la del pedido (la /6 la vuelve a poner en +0x10).
void CreateNoriaParticle(Particle& particle, float scale, Render::Models::CrtRandom& random);
// La 5c5/6 pide la posición de su dueño (`ownerStates`): sin él, MissingOwner después de moverse.
[[nodiscard]] Core::Expected<void, ParticleError> StepNoriaParticle(Particle& particle,
                                                                    const ParticleTerrainHeight& terrainHeight,
                                                                    const ParticleOwnerStates& ownerStates);
// 0x546c0f..0x546c9e: sin la tabla de los sprites del frame (`sprites`), MissingSprites después de girar, sacar su
// rand(), moverse y apagarse, antes del sprite.
[[nodiscard]] Core::Expected<void, ParticleError>
StepTheFallingShine(Particle& particle, Render::Models::CrtRandom& random, FrameSprites* sprites);
} // namespace Client::World
