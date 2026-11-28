#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::World
{
class IParticleTrigonometry;

// El fuego 0x5da (MissileFireParticle) en 0x53fe70 (0x540d13..0x54109e, una cadena de cmp por el subtipo) y en
// 0x5446e0 (0x545565..0x5456fc); el dibujo de 0x548550 los lleva a todos al billboard común (0x54889a). La /8 y la /9
// tienen su caso en Particles.cpp; aquí los demás. Los que no son de 0 a 9 sólo tienen el giro del final.
// La /0 (0x541045, la del alta del 0x116/1 en 0x51cb38) y la /3 (0x540d9d) suben y se agrandan con +0x50; la /1
// (0x540d1b) sólo sube; la /2 (0x540d5c) y la /7 (0x540f51) se apagan con la vida.
inline constexpr std::int32_t MissileTrailSubtype = 0;
inline constexpr std::int32_t MissileBurstSubtype = 1;
inline constexpr std::int32_t MissileDriftSubtype = 2;
inline constexpr std::int32_t MissileLongTrailSubtype = 3;
// La /4 (0x540dcf) da vueltas de radio 120 alrededor de donde nació; la /5 (0x540e74, la de 0x538f8e) avanza 20 hacia
// su ángulo z crudo; la /6 (0x540ee7) sube y gira 4.
inline constexpr std::int32_t MissileOrbitSubtype = 4;
inline constexpr std::int32_t MissileSwirlSubtype = 5;
inline constexpr std::int32_t MissileRiseSubtype = 6;
inline constexpr std::int32_t MissileFadeSubtype = 7;

// La 0x5da con un subtipo que no es la /8 ni la /9.
[[nodiscard]] bool IsMissileFireKernel(std::int32_t type, std::int32_t subtype);
// Después del prefijo de 0x53fe70 (Initialize, con la escala del pedido en +0x10).
void CreateMissileFire(Particle& particle, Crt::CrtRandom& random);
// Después de la vida y el movimiento comunes de 0x5446e0. La /4 y la /5 piden la trigonometría (fsin y fcos); sin
// ella, MissingClock.
[[nodiscard]] Core::Expected<void, ParticleError> StepMissileFire(Particle& particle,
                                                                  IParticleTrigonometry* trigonometry);

} // namespace Client::World
