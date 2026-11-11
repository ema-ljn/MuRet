#include "Scenes/GameRibbonCreation.h"

#include "Core/Expected.h"

#include "Scenes/GameDrawTail.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
std::int32_t Decrement(std::int32_t value)
{
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value) - 1u);
}
bool Shift(RibbonState& ribbon)
{
    if (ribbon.count >= static_cast<std::int32_t>(RibbonPointCount))
        return false;
    for (std::int32_t index = ribbon.count; index > 0; --index)
    {
        const auto at = static_cast<std::size_t>(index);
        ribbon.first[at] = ribbon.first[at - 1];
        ribbon.second[at] = ribbon.second[at - 1];
    }
    return true;
}
Core::Expected<std::optional<std::size_t>, std::string> Append(std::span<RibbonState> ribbons, std::size_t index,
                                                               const RibbonCreateRequest& request)
{
    auto& ribbon = ribbons[index];
    ribbon.type = request.type;
    ribbon.light = request.light;
    if (!Shift(ribbon))
        return Core::Unexpected(std::string("0x52e3d5: la historia de la cinta excede sus30 puntos"));
    ribbon.first[0] = request.first;
    ribbon.second[0] = request.second;
    constexpr std::int32_t LastCount = static_cast<std::int32_t>(RibbonPointCount) - 1;
    ribbon.count = ribbon.count < LastCount ? ribbon.count + 1 : LastCount;
    return std::optional<std::size_t>(index);
}
} // namespace
Core::Expected<std::optional<std::size_t>, std::string> CreateRibbon(std::span<RibbonState> ribbons,
                                                                     const RibbonCreateRequest& request)
{
    for (std::size_t index = 0; index < ribbons.size(); ++index)
    {
        const auto& ribbon = ribbons[index];
        if (ribbon.alive && ribbon.ownerContext == request.ownerContext &&
            (request.key <= 0 || ribbon.key == request.key))
            return Append(ribbons, index, request);
    }
    for (std::size_t index = 0; index < ribbons.size(); ++index)
    {
        auto& ribbon = ribbons[index];
        if (ribbon.alive)
            continue;
        ribbon.ownerContext = request.ownerContext;
        ribbon.ownerEquipment = request.ownerEquipment;
        ribbon.alive = true;
        ribbon.count = 0;
        ribbon.key = request.key;
        ribbon.life = request.shortTrail ? 15 : 30;
        return Append(ribbons, index, request);
    }
    return std::optional<std::size_t>{};
}
Core::Expected<void, std::string> StepRibbons(std::span<RibbonState> ribbons)
{
    for (auto& ribbon : ribbons)
    {
        if (!ribbon.alive)
            continue;
        ribbon.life = Decrement(ribbon.life);
        ribbon.count = Decrement(ribbon.count);
        if (ribbon.count > 0 && !Shift(ribbon))
            return Core::Unexpected(std::string("0x52e522: la historia de la cinta excede sus30 puntos"));
        if (ribbon.life > 0)
            continue;
        ribbon.count = Decrement(ribbon.count);
        if (ribbon.count <= 0)
            ribbon.alive = false;
    }
    return {};
}
} // namespace Client::Scenes
