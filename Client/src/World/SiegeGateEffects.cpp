#include "Kernel/ModelEffectArguments.h"
#include "World/SiegeGateEffects.h"
#include "Core/Expected.h"
#include "World/ModelEffects.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
namespace Client::World
{
bool IsCastleGateEffect(std::int16_t type)
{
    return type >= 0x3d && type <= 0x40;
}
Core::Expected<std::optional<std::size_t>, std::string>
CreateCastleGateEffect(const Kernel::ModelEffectArguments& arguments, ModelEffects& effects)
{
    if (!IsCastleGateEffect(arguments.type))
        return Core::Unexpected(std::string("0x51c05e: tipo ajeno a la puerta del castillo"));
    const auto slot = effects.Begin(arguments);
    if (slot)
        effects.Effects()[*slot].life = 2;
    return slot;
}
} // namespace Client::World
