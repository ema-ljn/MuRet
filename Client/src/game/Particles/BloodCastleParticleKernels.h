#pragma once

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
struct Particle;

// The particles of Blood Castle's torch 0x25 (0x604171..0x6042ac): the spark 6ab/0 and 6ab/1 (Effect\fi01.jpg,
// 0x61d55e), the flame 6ac/0 (Effect\fi02.tga, 0x61d57a) and the smoke 622/6 (Effect\clouds.jpg). The same torch's
// orbit 60f/4 goes with the 60f/3, in Particles.cpp. Contract: 06 § Los objetos de Devil Square y de Blood Castle.
inline constexpr std::int32_t TorchSparkParticle = 0x6ab;
inline constexpr std::int32_t TorchFlameParticle = 0x6ac;
// The 6ac/2 (0x53c042, in the step of joints 0x535840, from joint 0x632/6): drawn with GL_ADD (0x5489eb).
inline constexpr std::int32_t BrightFlameSubtype = 2;
inline constexpr std::int32_t TorchSmokeSubtype = 6;
[[nodiscard]] bool IsBloodCastleParticle(std::int32_t type, std::int32_t subtype);

// 0x5436fe (6ab): life rand() % 5 + 20 and turn 0; with subtype 0, the scale (rand() % 10) · 0.02f + 0.5 and the
// speed ((rand() % 10 + 5) · 0.4f; (rand() % 10 − 5) · 0.4f; (rand() % 10 + 5) · 0.2f); with another, the scale
// (rand() % 10) · 0.1f + 1 and the speed with 0.2f, 0.2f and 0.1f.
// 0x543a5d (6ac/0, 6ac/1 and 6ac/2): life rand() % 5 + 25, scale 0.5, turn rand() % 360 and speed ((rand() % 10 + 5) ·
// 0.4f; 0; (rand() % 10 + 5) · 0.2f); then it falls into the 6ec case (0x543ae3), which leaves the life at 24 and
// rolls the x again ((rand() % 10 − 5) · 0.4f) and the z ((rand() % 10 + 5) · 0.2f); the start (+0x6c) stays at the
// position. The /1 only changes its initial scale to argument·0.5 (0x543a84). 0x54327b (622/6): life rand() % 5 + 25,
// scale 0.2f, turn rand() % 360 and the speed of the 6ac before the 6ec. Each x87 product is stored in a float.
void CreateBloodCastleParticle(Particle& particle, Crt::CrtRandom& random);

// After the common life and movement of 0x5446e0, the three add the speed to the position and brake the x and the y
// by 0.95f. 0x5477e4 (6ab): the light at life · 0.1f, the z speed +0.3f and the scale +0.07f (subtype 0) or +0.09f.
// 0x54793f (6ac) and 0x54748a (622/6): the z speed +0.6f, the position shifted (rand() & 3) − 2 in x and y and
// ((rand() & 3) − 2) · 0.8f in z, the scale +0.05f, the light at life · 0.04f (6ac/0), life · 0.08f − 1 (6ac/2) or
// life · 0.02f (622/6), and the turn + (rand() & 1) + 1.
void StepBloodCastleParticle(Particle& particle, Crt::CrtRandom& random);
} // namespace Client::Particles
