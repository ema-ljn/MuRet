#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class Particles;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The hand shine of the skill that hits several: 0x53d810 and 0x53fdb0 over the 0x5fd of Particles::Particles (06, "La
// skill que pega a varios").
namespace Client::Effects
{

// 0x53fdb0 with the 0x5fd: the point (0, 0, 0) of a bone in the world, 0x4ec110 with Translate at 1 over the own bones
// (+0x120) of the owner `owner`, with its position (which 0x53fdb0 copies to the model's +0x6c) and its scale (the
// model's +0x68, left by whoever used it last: Q-C853). The bone is the hand (subtype % 2) of the hero `hero`
// (0x743a170 + 0x2bd + (subtype % 2) · 0x20), not the owner's (Q-C854). Without own bones (before the owner's first
// drawing), the zero matrix (Q-C318).
[[nodiscard]] glm::vec3 HandShinePoint(std::span<const Kernel::ActorState> actors, std::size_t hero, std::size_t owner,
                                       std::int32_t subtype);

// The 0x5fd step's resolver (Particles::ParticleActorHands): HandShinePoint of the pool slot among `actors`, which
// must outlive it; nullopt outside the pool.
[[nodiscard]] std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>
ParticleActorHandsAmong(std::span<const Kernel::ActorState> actors, std::size_t hero);

// 0x53d810(character, 0): the point (0, 0, 0) of the right-hand bone of the character `slot` (+0x2bd, with its own
// bones, its position and its scale: Q-C853) and, from there, with its angles (+0x1c), the light (1; 0.5; 0.2) and
// scale 0, the 0x5fd/0 and the 0x5fd/2 owned by the character (0x53fe70). Each one born goes to the hand with 0x53fdb0
// (HandShinePoint); with the pool full it is not born and there are no more. Without rand().
[[nodiscard]] Core::Expected<void, std::string> RaiseTheHandShine(std::span<const Kernel::ActorState> actors,
                                                                  std::size_t hero, std::size_t slot,
                                                                  Particles::Particles& particles,
                                                                  Crt::CrtRandom& random);
} // namespace Client::Effects
