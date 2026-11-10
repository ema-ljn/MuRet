#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/SkillTable.h"
#include "GameLogic/PathCell.h"
#include "Protocol/Movement.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::Scenes
{
class WorldState;
struct HeroMovementInput
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    bool leftPressed = false;
    bool leftDown = false;
    bool shiftDown = false;
    float worldTime = 0.0f;
    std::uint32_t ticksMs = 0;
};
struct HeroMovementContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::SkillTable* skills = nullptr;
    const Data::Tables::GateTable* gates = nullptr;
    HeroMovementInput input{};
    // El servicio usa el rayo, rango y trapecio del dibujo anterior.
    std::function<Core::Expected<std::optional<GameLogic::PathCell>, std::string>()> pickTarget{};
    std::function<Core::Expected<void, std::string>(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell,
                                                    float)>
        sendWalk{};
    std::function<void(std::uint8_t)> requestGate{};
    float fixedHeight = 1200.0f;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Scenes
