#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Particles
{
class IParticleTrigonometry;

// The fire 0x5da (MissileFireParticle) in 0x53fe70 (0x540d13..0x54109e, a cmp chain by subtype) and in 0x5446e0
// (0x545565..0x5456fc); the draw of 0x548550 takes them all to the common billboard (0x54889a). The /8 and the /9 have
// their case in Particles.cpp; the others are here. Those not from 0 to 9 only have the final turn.
// The /0 (0x541045, the one the 0x116/1 creates at 0x51cb38) and the /3 (0x540d9d) rise and grow with +0x50; the /1
// (0x540d1b) only rises; the /2 (0x540d5c) and the /7 (0x540f51) fade with the life.
inline constexpr std::int32_t MissileTrailSubtype = 0;
inline constexpr std::int32_t MissileBurstSubtype = 1;
inline constexpr std::int32_t MissileDriftSubtype = 2;
inline constexpr std::int32_t MissileLongTrailSubtype = 3;
// The /4 (0x540dcf) circles with radius 120 around where it was born; the /5 (0x540e74, that of 0x538f8e) advances 20
// toward its raw z angle; the /6 (0x540ee7) rises and turns 4.
inline constexpr std::int32_t MissileOrbitSubtype = 4;
inline constexpr std::int32_t MissileSwirlSubtype = 5;
inline constexpr std::int32_t MissileRiseSubtype = 6;
inline constexpr std::int32_t MissileFadeSubtype = 7;

// The 0x5da with a subtype that is neither the /8 nor the /9.
[[nodiscard]] bool IsMissileFireKernel(std::int32_t type, std::int32_t subtype);
// After the prefix of 0x53fe70 (Initialize, with the requested scale in +0x10).
void CreateMissileFire(Particle& particle, Crt::CrtRandom& random);
// After the common life and movement of 0x5446e0. The /4 and the /5 ask for the trigonometry (fsin and fcos); without
// it, MissingClock.
[[nodiscard]] Core::Expected<void, ParticleError> StepMissileFire(Particle& particle,
                                                                  IParticleTrigonometry* trigonometry);

} // namespace Client::Particles
