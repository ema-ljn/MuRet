#include "Kernel/WorldRuntime.h"

#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

namespace Client::Kernel
{
void WorldRuntime::RequestSound(Audio::SoundId id, std::optional<glm::vec3> origin, bool loop)
{
    lastSoundId = id;
    if (audio == nullptr)
    {
        return;
    }
    audio->PlaySound(id.Value(), origin, loop);
}

void WorldRuntime::StopSound(Audio::SoundId id, bool rewind)
{
    if (audio == nullptr)
    {
        return;
    }
    audio->StopSound(id.Value(), rewind);
}

void WorldRuntime::SetSoundVolume(std::uint8_t level)
{
    if (audio == nullptr)
    {
        return;
    }
    audio->SetEffectsVolume(level);
}
} // namespace Client::Kernel
