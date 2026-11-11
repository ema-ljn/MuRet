#pragma once

#include "Core/Expected.h"
#include "World/Particles.h"

#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
}

// Las partículas de los objetos de la selección (mapa 54, también los del 33) y del aura del elegido:
// 622/1 (la neblina de 0x425260 con dueño, 0x5435cf y 0x5476f0), 5de/7 (la llama del 0x43, 0x54150e y
// 0x545e3f) y 729/0,1 (las dos luces de 0x6348c6/0x6348dc, 0x543840 y 0x547880); y la 5de/0 del chorro de
// Lost Tower (0x54143c), que sólo sube. Intermedios double
// (PC53) y stores binary32, como los demás kernels. Contrato: 06 § El mundo de la selección.
namespace Client::World
{
inline constexpr std::int32_t FlameParticle = 0x5de;
inline constexpr std::int32_t ChaseLightParticle = 0x729;
// 0x543851: el 729/1 se dibuja con Effect\Impack03.jpg (0x719) de SelectionTextureSet.
inline constexpr std::int32_t ChaseLightImpactTexture = 0x719;
inline constexpr std::int32_t OwnedMistSubtype = 1;
inline constexpr std::int32_t RisingFlameSubtype = 7;
// 0x54143c: la llama del chorro 0x5de de Lost Tower (el 0x18 del mapa 4), que no es de la selección pero
// comparte el tipo y el dibujo.
inline constexpr std::int32_t FloorFlameSubtype = 0;

[[nodiscard]] bool IsSelectionParticle(std::int32_t type, std::int32_t subtype);
// Después del prefijo de 0x53fe70 (Initialize): `scaleArgument` es el argumento de escala, que 622/1 suma
// a +0x60 después de pisar +0x10. 622/1 consume siete rand(), 5de/7 cinco o seis, 5de/0 dos y 729 seis.
void CreateSelectionParticle(Particle& particle, float scaleArgument, Render::Models::CrtRandom& random);
// Después de vida/movimiento de 0x5446e0, incluso en el tick que apaga alive. 622/1 lee su dueño (+0x40,
// un objeto del mundo: +0x16c y +0x58) y el reloj del mundo; sin ellos, MissingOwner o MissingClock antes
// de escribir nada.
[[nodiscard]] Core::Expected<void, ParticleError> StepSelectionParticle(Particle& particle, const float* worldClock,
                                                                        IParticleTrigonometry* trigonometry,
                                                                        const ParticleObjectOwners& owners);
} // namespace Client::World
