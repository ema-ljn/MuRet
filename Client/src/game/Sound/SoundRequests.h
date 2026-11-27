#pragma once

#include "Audio/Playback.h"
#include "Audio/SoundId.h"
#include "Kernel/ISoundRequests.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

// Kernel::ISoundRequests over the device that plays the catalog's sounds (0x41ed10, 0x41ee00). Without a device, every
// request is dropped.
namespace Client::Sound
{
class SoundRequests final : public Kernel::ISoundRequests
{
public:
    explicit SoundRequests(Audio::IPlayback* playback);
    void Request(Audio::SoundId id, std::optional<glm::vec3> origin, bool loop) override;
    void Stop(Audio::SoundId id, bool rewind) override;
    void SetEffectsVolume(std::uint8_t level) override;

private:
    Audio::IPlayback* m_playback;
};
} // namespace Client::Sound
