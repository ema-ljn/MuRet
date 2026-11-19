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
// Los humos 5f2 de la CL11 con caso propio en la tabla del alta de 0x53fe70 y en la del paso de 0x5446e0 (las dos
// sólo del tipo 0x5f2, Effect\smoke01.jpg). El dibujo de 0x548550 ya los trata por su subtipo (0x548707..0x54874c).
// La /17 (0x5418b1) es la del 0x192 y el 0x197 (0x43bf75, 0x43c04b): guarda la luz y su paso la usa.
inline constexpr std::int32_t GlowSmokeSubtype = 17;
// La /19 (0x54228c, la del joint de 0x539494) y la /20 (0x542307, que se dibuja restando: 0x548732) tiemblan y crecen
// al azar.
inline constexpr std::int32_t ShakingSmokeSubtype = 19;
inline constexpr std::int32_t DarkShakingSmokeSubtype = 20;
// La /23 (0x54184b, el alta del aliento) es la de 0x44267f, 0x44274b y 0x442817: el aliento con otra luz.
inline constexpr std::int32_t WarmBreathSubtype = 23;
// La /24 (0x542450) es la de la piedra 0x12e/3 (0x529181, 0x5291c1).
inline constexpr std::int32_t StoneSmokeSubtype = 24;
// La /26 (0x54252c, la del pie del 0x1ba en 0x4271d0) se apaga con la vida; la /31 (0x5426cf) se achica hasta morir.
inline constexpr std::int32_t FadingSmokeSubtype = 26;
inline constexpr std::int32_t ShrinkingSmokeSubtype = 31;
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

// Después del prefijo de 0x53fe70 (Initialize, con la escala del pedido en +0x10). `clock` es el reloj del mundo
// (0x5767b88) que la /17, /19, /20, /23 y /24 y la 0x5f5 pasan por _ftol para el giro.
void CreateSmokeParticle(Particle& particle, Render::Models::CrtRandom& random, float clock);
// Después de la vida y el movimiento comunes de 0x5446e0. La /36 pide la posición de su dueño mientras su vida es 23 o
// más; sin él, MissingOwner.
[[nodiscard]] Core::Expected<void, ParticleError>
StepSmokeParticle(Particle& particle, Render::Models::CrtRandom& random, const ParticleOwnerStates& ownerStates);

} // namespace Client::World
