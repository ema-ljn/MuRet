#pragma once

#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

// What a module asks of the sound: a sound of the catalog, where it sounds from (none: at the listener) and whether it
// loops; to stop one, rewound or not; and the effects' volume. Sound implements it.
namespace Client::Kernel
{
class ISoundRequests
{
public:
    virtual ~ISoundRequests() = default;
    virtual void Request(Audio::SoundId id, std::optional<glm::vec3> origin, bool loop) = 0;
    virtual void Stop(Audio::SoundId id, bool rewind) = 0;
    virtual void SetEffectsVolume(std::uint8_t level) = 0;
};
} // namespace Client::Kernel
