#pragma once

#include "Audio/ISoundCatalog.h"
#include "Audio/SoundDefinition.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Audio
{
[[nodiscard]] std::optional<SoundDefinition> FindSound(std::int32_t id);
[[nodiscard]] std::optional<SoundDefinition> FindSound(std::int32_t id, std::string_view path);
[[nodiscard]] std::span<const SoundDefinition> StartupSounds();
[[nodiscard]] std::span<const SoundDefinition> WorldSoundsForMap(std::int32_t map);
[[nodiscard]] const ISoundCatalog& GameSoundCatalog();
} // namespace Client::Audio
