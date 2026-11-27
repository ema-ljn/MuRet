#include "Sound/SoundRequests.h"

#include "Audio/Playback.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

namespace Client::Sound
{
SoundRequests::SoundRequests(Audio::IPlayback* playback) : m_playback(playback) {}

void SoundRequests::Request(Audio::SoundId id, std::optional<glm::vec3> origin, bool loop)
{
    if (m_playback != nullptr)
        m_playback->PlaySound(id.Value(), origin, loop);
}

void SoundRequests::Stop(Audio::SoundId id, bool rewind)
{
    if (m_playback != nullptr)
        m_playback->StopSound(id.Value(), rewind);
}

void SoundRequests::SetEffectsVolume(std::uint8_t level)
{
    if (m_playback != nullptr)
        m_playback->SetEffectsVolume(level);
}
} // namespace Client::Sound
