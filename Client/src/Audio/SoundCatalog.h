#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Audio
{
struct SoundDefinition
{
    std::int32_t id;
    std::string_view path;
    std::uint8_t voices;
    bool positional;
};

[[nodiscard]] std::optional<SoundDefinition> FindSound(std::int32_t id);
[[nodiscard]] std::optional<SoundDefinition> FindSound(std::int32_t id, std::string_view path);
[[nodiscard]] std::span<const SoundDefinition> StartupSounds();
[[nodiscard]] std::span<const SoundDefinition> WorldSoundsForMap(std::int32_t map);
} // namespace Client::Audio
