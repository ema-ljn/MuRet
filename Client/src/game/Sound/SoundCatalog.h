#pragma once

#include "Audio/ISoundCatalog.h"
#include "Audio/SoundDefinition.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Sound
{
[[nodiscard]] std::optional<Audio::SoundDefinition> FindSound(std::int32_t id);
[[nodiscard]] std::optional<Audio::SoundDefinition> FindSound(std::int32_t id, std::string_view path);
[[nodiscard]] std::span<const Audio::SoundDefinition> StartupSounds();
[[nodiscard]] std::span<const Audio::SoundDefinition> WorldSoundsForMap(std::int32_t map);
[[nodiscard]] const Audio::ISoundCatalog& GameSoundCatalog();
} // namespace Client::Sound
