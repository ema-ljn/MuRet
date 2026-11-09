#pragma once

#include <cstdint>

namespace Client::World
{
// Errores provisionales de binding del port, no retornos de604ab0/52f360. Sin rollback.
enum class WorldObjectStepError : std::uint8_t
{
    UnsupportedParticle,
    UnsupportedModelEffect,
    UnsupportedMapHandler,
    UnsupportedSound,
};
} // namespace Client::World
