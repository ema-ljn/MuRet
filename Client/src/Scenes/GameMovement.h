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
    // CL9 hito 11, SendMove con 0x78abb00 (0x56b4db..0x56b78f): el C1 03 97 en claro (sin el servicio, corta), y
    // SetCursorPos ([0x6b84f4]) con el lugar que calcula el original en la pantalla de `screenWidth` × `screenHeight`
    // ([0x6d5d60] y [0x6d5d64]), que queda pedido en GameMovementState::cursorWarpRequest.
    std::function<Core::Expected<void, std::string>()> sendEventWindowLeave{};
    // El C1 03 31 de cerrar el NPC de evento al caminar (0x56b4d6 → 0x65cae0(0x1000), 0x658a18). Vacío, cerrarlo corta.
    std::function<void()> sendWindowClose{};
    std::int32_t screenWidth = 640;
    std::int32_t screenHeight = 480;
    // Vestir al héroe y el envío del 24, para pasar el arma de la izquierda a la derecha (0x59bd3e). nullptr: corta
    // donde los pide.
    const InventoryServices* inventory = nullptr;
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Scenes
