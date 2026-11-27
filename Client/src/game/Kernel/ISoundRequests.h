#pragma once

#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <optional>

// A sound a module asks for: the id of the sound catalog, where it sounds from (none: at the listener) and whether it
// loops. Sound implements it.
namespace Client::Kernel
{
class ISoundRequests
{
public:
    virtual ~ISoundRequests() = default;
    virtual void Request(Audio::SoundId id, std::optional<glm::vec3> origin, bool loop) = 0;
};
} // namespace Client::Kernel
