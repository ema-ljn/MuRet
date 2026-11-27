#include "Kernel/WorldRuntime.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>

namespace Client::Kernel
{
void WorldRuntime::RequestSound(std::int32_t id, std::optional<glm::vec3> origin, bool loop)
{
    lastSoundId = id;
    if (audio == nullptr)
    {
        return;
    }
    audio->PlaySound(id, origin, loop);
}

void WorldRuntime::StopSound(std::int32_t id, bool rewind)
{
    if (audio == nullptr)
    {
        return;
    }
    audio->StopSound(id, rewind);
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
