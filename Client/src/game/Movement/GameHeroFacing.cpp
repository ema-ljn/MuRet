#include "Movement/GameHeroFacing.h"

#include "Core/Expected.h"
#include "Math/OriginalAngles.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Math/Ftol.h"

#include <bit>
#include <cstdint>
#include <string>

namespace Client::Movement
{
Core::Expected<void, std::string> StepHeroFacing(const HeroFacingContext& context)
{
    Kernel::ActorState& hero = context.hero;
    GameMovementState& state = context.movement;
    if (hero.movement != 0 || state.inputBlocked)
        return {};
    state.movementTarget = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.movementTarget) + 1u);
    // 0x59b357..0x59b367: 0x65972c → 0x6570a0, guild window 2, reads a plain byte, without a logical close or rand().
    if (!context.secondGuildOpen && state.facingMode == 1)
        return {};
    if (state.movementTarget < 40 || context.mouseOnUi != 0 || hero.dying != 0)
        return {};
    const auto action = hero.animation.action;
    if (action == 0xcb || action == 0xcc || action == 0xc5 || action == 0xc7 || !context.headTurningAllowed ||
        action == 0x4d || action == 0x4f || action == 0x5a || hero.byte381 != 0)
        return {};
    state.movementTarget = 0;
    const auto angle =
        Math::OriginalFtolLow(Math::CreateAngle(static_cast<float>(context.mouseX), static_cast<float>(context.mouseY),
                                                static_cast<float>(context.centerX), 180.0f));
    if (!angle)
        return Core::Unexpected(std::string("0x59b422: ángulo quieto fuera del perfil ftol"));
    const auto shifted = std::bit_cast<std::int32_t>(405u - std::bit_cast<std::uint32_t>(*angle));
    state.arrivalYaw = shifted % 360;
    // 0x59b456: the same calculation as the C1 18 of the emote and of sitting.
    const auto oldSector = Actors::ActionDirection(hero.angles.z);
    const auto newSector = Actors::ActionDirection(static_cast<float>(state.arrivalYaw));
    if (!oldSector || !newSector)
        return Core::Unexpected(std::string("0x59b456: sector quieto fuera del perfil ftol"));
    if (*oldSector == *newSector)
        return {};
    if (action != 0x39)
        hero.angles.z = static_cast<float>(state.arrivalYaw);
    if (!context.sendFacing)
        return Core::Unexpected(std::string("0x59b74c: falta el servicio de envío 18/7A"));
    return context.sendFacing(*newSector);
}
} // namespace Client::Movement
