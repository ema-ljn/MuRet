#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/SkillTable.h"
#include "GameLogic/PathCell.h"
#include "Protocol/Movement.h"
#include "Scenes/ActionSend.h"
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
struct InventoryServices;
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
    // El D7 de pegar al llegar (0x5650c0, CL9 hito 3): la clave del blanco, la acción y el rumbo. Vacío, no manda.
    std::function<Core::Expected<void, std::string>(std::uint16_t, std::uint8_t, std::uint8_t)> sendAttack{};
    // El C1 18 de sentarse del héroe (0x4f2250, 0x4f2416..0x4f2bea). Vacío: sentarse corta con error.
    ActionSender sendAction{};
    // Vestir al héroe y el envío del 24, para pasar el arma de la izquierda a la derecha (0x59bd3e). nullptr: corta
    // donde los pide.
    const InventoryServices* inventory = nullptr;
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Scenes
