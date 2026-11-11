#include "Scenes/GameHeroFacing.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "World/OriginalFtol.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t FacingWindow = 0x40000;
constexpr float HalfSector = 22.5f;
constexpr float SectorReciprocal = 0.02222222276031971f;

std::optional<std::uint8_t> FacingSector(float yaw)
{
    const auto whole = World::OriginalFtolLow((static_cast<double>(yaw) + HalfSector) * SectorReciprocal + 1.0);
    if (!whole)
        return std::nullopt;
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(*whole) & 7u);
}
} // namespace

Core::Expected<void, std::string> StepHeroFacing(const HeroFacingContext& context)
{
    ActorState& hero = context.hero;
    GameMovementState& state = context.movement;
    if (hero.movement != 0 || state.inputBlocked)
        return {};
    state.movementTarget = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.movementTarget) + 1u);
    // 65972c -> 6570a0: ventana40000 lee un byte plano, sin cierre lógico ni RNG.
    if ((state.windowsMask & FacingWindow) == 0 && state.facingMode == 1)
        return {};
    if (state.movementTarget < 40 || state.facingBlocked != 0 || hero.dying != 0)
        return {};
    const auto action = hero.animation.action;
    if (action == 0xcb || action == 0xcc || action == 0xc5 || action == 0xc7 || !context.headTurningAllowed ||
        action == 0x4d || action == 0x4f || action == 0x5a || hero.byte381 != 0)
        return {};
    state.movementTarget = 0;
    const auto angle = World::OriginalFtolLow(Render::Models::CreateAngle(static_cast<float>(context.mouseX),
                                                                          static_cast<float>(context.mouseY),
                                                                          static_cast<float>(context.centerX), 180.0f));
    if (!angle)
        return Core::Unexpected(std::string("0x59b422: ángulo quieto fuera del perfil ftol"));
    const auto shifted = std::bit_cast<std::int32_t>(405u - std::bit_cast<std::uint32_t>(*angle));
    state.arrivalYaw = shifted % 360;
    const auto oldSector = FacingSector(hero.angles.z);
    const auto newSector = FacingSector(static_cast<float>(state.arrivalYaw));
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
} // namespace Client::Scenes
