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

// The 5f2 smokes with their own case in 0x53fe70's creation table and in 0x5446e0's step table (both only of type
// 0x5f2, Effect\smoke01.jpg). 0x548550's drawing already handles them by subtype (0x548707..0x54874c). /13 (0x541c18)
// is that of Kundun's staff 0x192 (0x43c9cb), of 0x91's 0x195 (0x43e4c0) and of arrow 0x111 (0x525972, 0x52598b,
// 0x5259a4): born with random scale, turn and rise; its step (0x5462c8) lights it orange with life and enlarges it.
inline constexpr std::int32_t EmberSmokeSubtype = 13;
// /17 (0x5418b1) is that of 0x192 and 0x197 (0x43bf75, 0x43c04b): it keeps the light and its step uses it.
inline constexpr std::int32_t GlowSmokeSubtype = 17;
// /19 (0x54228c, created by the joints' step at 0x5394fb) and /20 (0x542307, drawn subtractively: 0x548732) shake and
// grow at random.
inline constexpr std::int32_t ShakingSmokeSubtype = 19;
inline constexpr std::int32_t DarkShakingSmokeSubtype = 20;
// /23 (0x54184b, the breath's creation) is that of 0x44267f, 0x44274b and 0x442817: the breath with another light.
inline constexpr std::int32_t WarmBreathSubtype = 23;
// /24 (0x542450) is that of stone 0x12e/3 (0x529181, 0x5291c1).
inline constexpr std::int32_t StoneSmokeSubtype = 24;
// /26 (0x54252c, that of 0x1ba's foot at 0x4271d0) fades with life; /31 (0x5426cf) shrinks until it dies.
inline constexpr std::int32_t FadingSmokeSubtype = 26;
inline constexpr std::int32_t ShrinkingSmokeSubtype = 31;
// The mist /28, /29 and /30 of the small storm 0xcf/3..7 (0x523144, 0x52315d and 0x523176): born on the ground, at the
// terrain height (0x5f93c0) plus 15, with turn rand() % 360, and rise with their +0x50. /28 (0x5425c7, life 12, scale
// 2.3f, rises 33) is drawn with texture 0x6ad and its step (0x546852) shrinks it 0.4f (with the already decremented
// life at 8 or less, enlarges it by 1.2f) and fades it by 0.92f; /29 (0x54261b) is the same with the type's texture,
// light at 1 (0x54689e) and drawn subtractively (0x548732); /30 (0x542668, life 4, scale 1.8f, rises 15) shifts in x
// and y by sin(clock) · 5 (0x5426ae) and its step (0x5468de) enlarges it 0.15f with light (1; 3; 1).
inline constexpr std::int32_t StormMistSubtype = 28;
inline constexpr std::int32_t DarkStormMistSubtype = 29;
inline constexpr std::int32_t ShortStormMistSubtype = 30;
// /36 and /37 are the trail of 0x52497d and 0x5249a0, which pass the effect as owner (+0x40): /36 follows its position
// the first two steps; /37 takes the requested light as a direction and moves back along it.
inline constexpr std::int32_t TrailSmokeSubtype = 36;
inline constexpr std::int32_t BackTrailSmokeSubtype = 37;
// 0x5f5 (creation 0x542943..0x542a5f, step 0x546a13..0x546aff): a smoke that keeps the requested light in
// +0x60..+0x68, rises accelerating, grows 0.03f and turns with its x angle ((rand() & 3) − 2). /0 (life 55, the joint's
// of 0x539058) lowers the kept red; /1 (life 30) rises slower; /2 (life 55, the effect's of 0x52a5f1) lowers the green
// and the blue. The other subtypes only have the prefix. 0x548895 draws it blended (0x620c10) in the common billboard.
inline constexpr std::int32_t RisingPuffParticle = 0x5f5;

[[nodiscard]] bool IsSmokeParticle(std::int32_t type, std::int32_t subtype);
// 5f2/28..30, which request the terrain height at birth.
[[nodiscard]] bool IsStormMist(std::int32_t type, std::int32_t subtype);

// After 0x53fe70's prefix (Initialize, with the requested scale in +0x10). `clock` is the world clock (0x5767b88),
// which /17, /19, /20, /23, /24 and 0x5f5 pass through _ftol for the turn.
void CreateSmokeParticle(Particle& particle, Crt::CrtRandom& random, float clock);
// After 0x53fe70's prefix, the mist 5f2/28..30: its rand(), the terrain height (0x5f93c0) at its point and, for /30,
// the clock's sine (0x5767b88) through the trigonometry. Without the clock or the trigonometry /30 needs, MissingClock
// with the rand() taken and the z already set.
[[nodiscard]] Core::Expected<void, ParticleError> CreateStormMist(Particle& particle, Crt::CrtRandom& random,
                                                                  const ParticleTerrainHeight& terrainHeight,
                                                                  const float* worldClock,
                                                                  IParticleTrigonometry* trigonometry);
// After 0x5446e0's common life and movement. /36 requests its owner's position while its life is 23 or more; without
// it, MissingOwner.
[[nodiscard]] Core::Expected<void, ParticleError> StepSmokeParticle(Particle& particle, Crt::CrtRandom& random,
                                                                    const ParticleOwnerStates& ownerStates);

} // namespace Client::Particles
