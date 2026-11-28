#pragma once

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
struct Particle;

// The spark 0x5c6/0xb (Spark03) released by bow 0xa27 (0x50af72) and staff 0xc1e (0x50b5d8) when drawing what hangs.
// 0x5c6/0xd shares its instructions in 0x53fe70 (0x540977) and 0x5446e0 (0x545368). It goes to the common billboard,
// like every 0x5c6.
inline constexpr std::int32_t WeaponSparkSubtype = 11;
inline constexpr std::int32_t TwinWeaponSparkSubtype = 13;
[[nodiscard]] bool IsWeaponSpark(std::int32_t type, std::int32_t subtype);

// 0x540977..0x5409fc, after the prefix: +0x38 at 1.0f, velocity ((rand() & 7) − 4; rand() & 3; rand() & 3), life 30
// between the y and the z, and scale (rand() % 5) · 0.1f + 1, which overwrites the argument's.
void CreateWeaponSpark(Particle& particle, Crt::CrtRandom& random);

// 0x545368..0x54535a, after 0x5446e0's common life and movement: light times 0.952381f (0x6ba264), scale −0.04f and
// +0x38 −0.001f. It dies when the difference, unrounded to float, is not above 0 (fst and fcomp, Q-C360).
void StepWeaponSpark(Particle& particle);

// The spark 0x5c6/0xf (Spark03) released by the Fenrir in action 4 from bone 14 (0x6028c6), with the scale from the
// clock's sine. It also goes to the common billboard.
inline constexpr std::int32_t FenrirSparkSubtype = 15;
[[nodiscard]] bool IsFenrirSpark(std::int32_t type, std::int32_t subtype);

// 0x540b42..0x540b89, after the prefix: one rand() and, with a remainder by 3 other than 0, nothing more (Q-C422: it
// keeps the prefix's life 2 and the slot's previous +0x38). With 0, +0x38 at 1.0f, life 35 and z velocity ((rand() % 5)
// + 5) · −0.5f, on the x87 until the fstp.
void CreateFenrirSpark(Particle& particle, Crt::CrtRandom& random);

// 0x5453d3..0x5453f7 and 0x545343..0x545363, after the common life and movement: light times 0.952381f (0x6ba264) and
// +0x38 −0.001f, without the scale; it dies like the bow's.
void StepFenrirSpark(Particle& particle);
} // namespace Client::Particles
