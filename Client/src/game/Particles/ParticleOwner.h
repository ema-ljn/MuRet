#pragma once

#include <cstdint>

namespace Client::Particles
{
// Non-owning identity, copyable with the frame. None is NULL; Actor uses the pool index, WorldObject its stable
// identity. Clearing the pools comes before releasing the map.
enum class ParticleOwnerKind : std::uint8_t
{
    None,
    Actor,
    WorldObject,
    ModelEffect,
};
struct ParticleOwner
{
    ParticleOwnerKind kind = ParticleOwnerKind::None;
    std::uint64_t identity = 0;
    friend bool operator==(const ParticleOwner&, const ParticleOwner&) = default;
};
} // namespace Client::Particles
