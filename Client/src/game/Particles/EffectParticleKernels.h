#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Particles
{
class IParticleTrigonometry;

// The particles of the CL11 effects that 0x53fe70 and 0x5446e0 handle in their own cases: the ring 0x600
// (Effect\ring.jpg), the smoke 5f2/1 and 5f2/3 (Effect\smoke01.jpg; the other CL11 smokes, in SmokeParticleKernels),
// the flame 5de/1..6 (Effect\Flame01.jpg; the /0 and the /7 belong to the selection) and the 5c6 sparks without a
// kernel of their own (Effect\Spark03.jpg). All go to the common billboard of 0x548550 (0x54889a; 5f2 through
// 0x54874c).
inline constexpr std::int32_t RingParticle = 0x600;
// 0x543183: the /1 (that of Kalima's 0x43a861, on the target) lives 15 and turns at random; the others live 20.
inline constexpr std::int32_t RingPulseSubtype = 0;
inline constexpr std::int32_t RingBurstSubtype = 1;
// 0x541eb6: the poison smoke (the ten of skill 1, 0x50497e) and 0x541cf2, that of effects 0x5216d6 and 0x521960.
inline constexpr std::int32_t PoisonSmokeSubtype = 1;
inline constexpr std::int32_t EffectSmokeSubtype = 3;
// 0x5417ba: the 5de/4 follows its owner (+0x40) from the position it had at birth.
inline constexpr std::int32_t FollowingFlameSubtype = 4;
// The 5c6 sparks (table of 0x5443c0 at creation and of 0x548204 in the step): the /0 of the hit (0x52e740), the /3 of
// the 0x45 lights (0x50c0db), the /4 that goes toward its owner and follows it, the /6 that is born only once in 50,
// the green /7, the /8 (the /0 with life 10) and the /10, which turns off with +0x38.
inline constexpr std::int32_t HitSparkSubtype = 0;
inline constexpr std::int32_t ShrinkingSparkSubtype = 3;
inline constexpr std::int32_t FollowingSparkSubtype = 4;
inline constexpr std::int32_t RareSparkSubtype = 6;
inline constexpr std::int32_t GreenSparkSubtype = 7;
inline constexpr std::int32_t ShortHitSparkSubtype = 8;
inline constexpr std::int32_t BurstSparkSubtype = 10;

[[nodiscard]] bool IsEffectParticle(std::int32_t type, std::int32_t subtype);
// Those that read +0x40 in their step (5c6/4, 5d5/2, the fire /9, 5de/4, 5f2/36, 5c5/5 and 5c5/6): 0x53fe70 stores the
// owner in the slot.
[[nodiscard]] bool FollowsItsOwner(std::int32_t type, std::int32_t subtype);
// Those that also read the owner's position at birth: 5c6/4 (0x540686), 5de/4 (0x5417ba) and 5c5/5 (0x5403ad).
[[nodiscard]] bool NeedsOwnerPosition(std::int32_t type, std::int32_t subtype);
// Those that read the terrain height at birth (0x5f93c0): the mist 5f2/28, /29 and /30 (0x5425f9, 0x54264d, 0x54269a).
[[nodiscard]] bool NeedsTerrainHeight(std::int32_t type, std::int32_t subtype);

// What the creation reads besides the slot: the world clock (0x5767b88) and its sine for the light of 5de/2..4 and the
// mist 5f2/30, the owner's position for 5c6/4 and 5de/4, the clock by value for those that do not lend it (the smokes
// of SmokeParticleKernels use it without `worldClock`) and the terrain height for the mist 5f2/28..30.
struct EffectParticleBirth
{
    const float* worldClock = nullptr;
    IParticleTrigonometry* trigonometry = nullptr;
    const glm::vec3* ownerPosition = nullptr;
    float legacyClock = 0.0f;
    const ParticleTerrainHeight* terrainHeight = nullptr;
};

// After the prefix of 0x53fe70 (Initialize). Without the clock or the trigonometry that 5de/2..4 and 5f2/30 ask for,
// MissingClock with the rand() already taken; without the owner's position, MissingOwner, and without the terrain
// height of 5f2/28..30, MissingTerrainHeight (Particles asks for them before touching the slot).
[[nodiscard]] Core::Expected<void, ParticleError> CreateEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                                       const EffectParticleBirth& birth);
// After the common life and movement of 0x5446e0. The ring asks for the trigonometry (fsin); 5c6/4 and 5de/4, their
// owner's state.
[[nodiscard]] Core::Expected<void, ParticleError> StepEffectParticle(Particle& particle, Crt::CrtRandom& random,
                                                                     IParticleTrigonometry* trigonometry,
                                                                     const ParticleOwnerStates& ownerStates);

} // namespace Client::Particles
