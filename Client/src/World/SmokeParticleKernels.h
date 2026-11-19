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
class IParticleTrigonometry;

// Los humos 5f2 de la CL11 con caso propio en la tabla del alta de 0x53fe70 y en la del paso de 0x5446e0 (las dos
// sólo del tipo 0x5f2, Effect\smoke01.jpg). El dibujo de 0x548550 ya los trata por su subtipo (0x548707..0x54874c).
// La /13 (0x541c18) es la del bastón de Kundun 0x192 (0x43c9cb), la del 0x195 del 0x91 (0x43e4c0) y la de la flecha
// 0x111 (0x525972, 0x52598b, 0x5259a4): nace con escala, giro y subida al azar y su paso (0x5462c8) la enciende con la
// vida en naranja y la agranda.
inline constexpr std::int32_t EmberSmokeSubtype = 13;
// La /17 (0x5418b1) es la del 0x192 y el 0x197 (0x43bf75, 0x43c04b): guarda la luz y su paso la usa.
inline constexpr std::int32_t GlowSmokeSubtype = 17;
// La /19 (0x54228c, la que el paso de los joints crea en 0x5394fb) y la /20 (0x542307, que se dibuja restando:
// 0x548732) tiemblan y crecen al azar.
inline constexpr std::int32_t ShakingSmokeSubtype = 19;
inline constexpr std::int32_t DarkShakingSmokeSubtype = 20;
// La /23 (0x54184b, el alta del aliento) es la de 0x44267f, 0x44274b y 0x442817: el aliento con otra luz.
inline constexpr std::int32_t WarmBreathSubtype = 23;
// La /24 (0x542450) es la de la piedra 0x12e/3 (0x529181, 0x5291c1).
inline constexpr std::int32_t StoneSmokeSubtype = 24;
// La /26 (0x54252c, la del pie del 0x1ba en 0x4271d0) se apaga con la vida; la /31 (0x5426cf) se achica hasta morir.
inline constexpr std::int32_t FadingSmokeSubtype = 26;
inline constexpr std::int32_t ShrinkingSmokeSubtype = 31;
// La niebla /28, /29 y /30 de la tormenta chica 0xcf/3..7 (0x523144, 0x52315d y 0x523176): nacen en el suelo, a la
// altura del terreno (0x5f93c0) más 15, con el giro rand() % 360 y suben con su +0x50. La /28 (0x5425c7, vida 12,
// escala 2,3f, sube 33) se dibuja con la textura 0x6ad y su paso (0x546852) la achica 0,4f (con la vida ya descontada
// en 8 o menos, la agranda por 1,2f) y la apaga por 0,92f; la /29 (0x54261b) es igual con la textura del tipo, la luz
// en 1 (0x54689e) y se dibuja restando (0x548732); la /30 (0x542668, vida 4, escala 1,8f, sube 15) se corre en x y en y
// con sin(reloj) · 5 (0x5426ae) y su paso (0x5468de) la agranda 0,15f con la luz (1; 3; 1).
inline constexpr std::int32_t StormMistSubtype = 28;
inline constexpr std::int32_t DarkStormMistSubtype = 29;
inline constexpr std::int32_t ShortStormMistSubtype = 30;
// La /36 y la /37 son la estela de 0x52497d y 0x5249a0, que le pasan el efecto como dueño (+0x40): la /36 sigue su
// posición los dos primeros pasos; la /37 toma la luz del pedido como dirección y retrocede por ella.
inline constexpr std::int32_t TrailSmokeSubtype = 36;
inline constexpr std::int32_t BackTrailSmokeSubtype = 37;
// La 0x5f5 (alta 0x542943..0x542a5f, paso 0x546a13..0x546aff): un humo que guarda la luz del pedido en +0x60..+0x68,
// sube acelerando, crece 0,03f y gira con su ángulo x ((rand() & 3) − 2). La /0 (vida 55, la del joint de 0x539058)
// baja el rojo guardado; la /1 (vida 30) sube más lento; la /2 (vida 55, la del efecto de 0x52a5f1) baja el verde y el
// azul. Los demás subtipos sólo tienen el prefijo. 0x548895 la dibuja mezclada (0x620c10) en el billboard común.
inline constexpr std::int32_t RisingPuffParticle = 0x5f5;

[[nodiscard]] bool IsSmokeParticle(std::int32_t type, std::int32_t subtype);
// La niebla 5f2/28..30, la que pide la altura del terreno al nacer.
[[nodiscard]] bool IsStormMist(std::int32_t type, std::int32_t subtype);

// Después del prefijo de 0x53fe70 (Initialize, con la escala del pedido en +0x10). `clock` es el reloj del mundo
// (0x5767b88) que la /17, /19, /20, /23 y /24 y la 0x5f5 pasan por _ftol para el giro.
void CreateSmokeParticle(Particle& particle, Render::Models::CrtRandom& random, float clock);
// Después del prefijo de 0x53fe70, la niebla 5f2/28..30: su rand(), la altura del terreno (0x5f93c0) en su punto y, la
// /30, el seno del reloj (0x5767b88) por la trigonometría. Sin el reloj o la trigonometría que pide la /30,
// MissingClock con el rand() sacado y la z ya puesta.
[[nodiscard]] Core::Expected<void, ParticleError> CreateStormMist(Particle& particle, Render::Models::CrtRandom& random,
                                                                  const ParticleTerrainHeight& terrainHeight,
                                                                  const float* worldClock,
                                                                  IParticleTrigonometry* trigonometry);
// Después de la vida y el movimiento comunes de 0x5446e0. La /36 pide la posición de su dueño mientras su vida es 23 o
// más; sin él, MissingOwner.
[[nodiscard]] Core::Expected<void, ParticleError>
StepSmokeParticle(Particle& particle, Render::Models::CrtRandom& random, const ParticleOwnerStates& ownerStates);

} // namespace Client::World
