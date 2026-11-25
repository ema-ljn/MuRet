#pragma once

#include "Audio/SoundDefinition.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Audio
{
class ISoundCatalog
{
public:
    virtual ~ISoundCatalog() = default;
    [[nodiscard]] virtual std::optional<SoundDefinition> Find(std::int32_t id, std::string_view path) const = 0;
    [[nodiscard]] virtual std::span<const SoundDefinition> StartupSounds() const = 0;
};
} // namespace Client::Audio
