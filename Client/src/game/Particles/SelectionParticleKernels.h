#pragma once

#include "Core/Expected.h"
#include "Particles/Particles.h"

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

// The particles of the selection objects (map 54, also those of 33) and of the chosen one's aura: 622/1 (0x425260's
// owned mist, 0x5435cf and 0x5476f0), 5de/7 (0x43's flame, 0x54150e and 0x545e3f) and 729/0,1 (the two lights of
// 0x6348c6/0x6348dc, 0x543840 and 0x547880); Lost Tower's jet 5de/0 (0x54143c), which only rises; and the clouds
// 622/0..5 of Icarus's objects (0x603f3b), with 622/1's creation and step and 0x54848c's per-subtype turn. Double
// intermediates (PC53) and binary32 stores, like the other kernels. Contract: 06 § El mundo de la selección and § Los
// objetos de Icarus.
namespace Client::Particles
{
inline constexpr std::int32_t FlameParticle = 0x5de;
inline constexpr std::int32_t ChaseLightParticle = 0x729;
// 0x543851: 729/1 is drawn with Effect\Impack03.jpg (0x719) from SelectionTextureSet.
inline constexpr std::int32_t ChaseLightImpactTexture = 0x719;
inline constexpr std::int32_t OwnedMistSubtype = 1;
inline constexpr std::int32_t RisingFlameSubtype = 7;
// 0x54143c: the flame of Lost Tower's 0x5de jet (map 4's 0x18), not from the selection but sharing the type and the
// drawing.
inline constexpr std::int32_t FloorFlameSubtype = 0;

[[nodiscard]] bool IsSelectionParticle(std::int32_t type, std::int32_t subtype);
// After 0x53fe70's prefix (Initialize): 622/1 adds `scaleArgument` to +0x60 after overwriting +0x10. 622/0..5 take
// seven rand(), 5de/7 five or six, 5de/0 two and 729 six.
void CreateSelectionParticle(Particle& particle, float scaleArgument, Crt::CrtRandom& random);
// After 0x5446e0's life/movement, even on the tick that clears alive. 622/0..5 read their owner (+0x40, a world object:
// +0x16c and +0x58) and the world clock; without them, MissingOwner or MissingClock before writing anything.
[[nodiscard]] Core::Expected<void, ParticleError> StepSelectionParticle(Particle& particle, const float* worldClock,
                                                                        IParticleTrigonometry* trigonometry,
                                                                        const ParticleObjectOwners& owners);
} // namespace Client::Particles
