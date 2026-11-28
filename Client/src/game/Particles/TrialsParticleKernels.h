#pragma once

#include "Core/Expected.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
struct Particle;
enum class ParticleError : std::uint8_t;

// The two particles that Land of Trials's objects (map 31) create at 0x4401e0: the splash 6f2/3 of 3
// (Effect\WATERFALL3.jpg, 0x440378) and the smoke 5f2/22 of 0x35 (Effect\smoke01.jpg, 0x44023a). 6f3/3 shares 6f2/3's
// instructions in 0x53fe70 and 0x5446e0. Both go to the common billboard.
inline constexpr std::int32_t TrialsSplashSubtype = 3;
inline constexpr std::int32_t TrialsSmokeSubtype = 22;
[[nodiscard]] bool IsTrialsParticle(std::int32_t type, std::int32_t subtype);

// 0x544319 (6f2/3): life 60, z velocity −1, scale (rand() % 10 + 15) · scale · 0.02f, turn rand() % 360 and the x and
// the z shifted rand() % 40 − 20 and rand() % 25 − 10. 0x5423e0 (5f2/22): life 60, scale ((rand() & 63) + 64) · scale ·
// 0.005f, turn rand() % 360 and vertical speed ((rand() & 31) + 60) · 0.1f.
void CreateTrialsParticle(Particle& particle, Crt::CrtRandom& random);

// After 0x5446e0's common life and movement. 0x54815d (6f2/3): scale +0.005f, z velocity −0.05f and light times 0.97f.
// 0x546706 (5f2/22): light (life · 0.02f) times (0.9; 0.5; 0.5), vertical speed −0.1f, x plus sin(clock) times that
// unrounded speed times 0.05f, z plus the speed and scale +0.04f. Without the world clock (0x5767b88), or with a sine
// outside the ported domain, 5f2/22 cuts before touching the x.
[[nodiscard]] Core::Expected<void, ParticleError> StepTrialsParticle(Particle& particle, const float* clock);
} // namespace Client::Particles
