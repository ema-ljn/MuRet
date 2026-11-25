#pragma once

#include "Render/TextureSlot.h"

#include <cstdint>
#include <optional>

namespace Client::Render
{
class ITextureSlots
{
public:
    virtual ~ITextureSlots() = default;
    [[nodiscard]] virtual std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const = 0;
};
} // namespace Client::Render
