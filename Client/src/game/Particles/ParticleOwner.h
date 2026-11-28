#pragma once

#include <cstdint>

namespace Client::Particles
{
// Identidad no poseedora, copiable con el frame. None es NULL; Actor usa índice de pool,
// WorldObject su identidad estable. Clear de los pools precede la liberación del mapa.
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
