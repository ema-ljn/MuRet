#include "Scenes/HeroSkillCasts.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/HudItems.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t ManaOffset = 0x22;
constexpr std::size_t StaminaOffset = 0x24;

std::uint16_t WordAt(std::span<const std::byte, Data::Tables::SkillRecordSize> record, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(record[offset]) |
                                      (std::to_integer<std::uint16_t>(record[offset + 1]) << 8));
}
} // namespace

Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                   std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: 0x551c70 sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    const Render::Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    return SkillCost{.mana = WordAt(*record, ManaOffset), .stamina = WordAt(*record, StaminaOffset)};
}

Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::span<const World::ItemState, InventoryCellCount> cells(context.world.Inventory().cells);
    const auto found =
        FindQuickSlotItem(runtime.hud.quickSlots, cells, ManaPotionSlot, runtime.movement.gateRequestBlocked != 0);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (!found.Value())
        return {};
    return Core::Unexpected(std::format("{}: la poción de maná de la casilla {} (0x5af520 y el C1 26) es del hito 5",
                                        where, *found.Value()));
}
} // namespace Client::Scenes
