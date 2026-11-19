#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <glm/ext/vector_float3.hpp>

namespace Client::Audio
{
class IPlayback
{
public:
    virtual ~IPlayback() = default;
    virtual void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) = 0;
    virtual void StopSound(std::int32_t id, bool rewind) = 0;
    virtual void PlayMusic(std::string_view relativePath, bool loop) = 0;
    virtual void StopMusic() = 0;
    virtual void SetEffectsVolume(std::uint8_t level) = 0;
    virtual void SetListener(glm::vec3 position) = 0;
    virtual void RegisterSound(std::int32_t, std::string_view) {}
    virtual void ReleaseWorldSounds() {}
    [[nodiscard]] virtual int MusicProgressPercent() const
    {
        return 0;
    }
};
} // namespace Client::Audio
