#include "Scenes/ItemTooltips.h"

#include "Core/Expected.h"
#include "Scenes/GameHud.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> DrawTheItemTooltip(const ItemTooltipContext& context)
{
    return Core::Unexpected(std::format("0x5b2aa0: el tooltip del ítem {:#x} es del hito 5", context.item.id));
}

Core::Expected<void, std::string> DrawTheRepairTooltip(const ItemTooltipContext& context)
{
    return Core::Unexpected(std::format("0x5b8150: el tooltip de reparar el {:#x} es del hito 5", context.item.id));
}

Core::Expected<void, std::string> DrawTheSkillTooltip(GameHudContext& context, std::int32_t x, std::int32_t y,
                                                      std::int32_t skill, std::int32_t flag)
{
    static_cast<void>(context);
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(flag);
    return Core::Unexpected(std::format("0x5b90b0: el tooltip de la skill {} es del hito 5", skill));
}
} // namespace Client::Scenes
