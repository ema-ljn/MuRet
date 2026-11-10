#pragma once
#include <cstdint>
#include <array>

// Globales durables de MoveHero y 1C: research06, Caminar y Los cambios de mapa.
namespace Client::Scenes
{
struct GameMovementState
{
    std::int32_t mouseUpdateTime = 0;
    // .data[6c8dcc] = 6 antes del primer SendMove.
    std::int32_t mouseUpdateTimeMax = 6;
    bool warpPending = false;
    std::int32_t gateRequestBlocked = 0;
    std::uint32_t lastTeleportMs = 0;
    std::int32_t arrivalYaw = 0;
    std::int32_t movementTarget = 0;
    std::int32_t facingMode = 0;    // 0x78abb18, cero desde la selección
    std::uint8_t facingBlocked = 0; // 0x775857c
    std::uint32_t windowsMask = 0;
    bool pendingClick = false;
    bool inputBlocked = false;
    bool movementBlocked = false;
    bool cameraTour = false;
    float heldSince = 0.0f;
    float heldElapsed = 0.0f;
    bool leftHeld = false;
    bool previousLeftDown = false;
    std::array<std::uint8_t, 256> skillDepths{};
    float selectedSkillRange = 0.0f;
    std::uint32_t targetX = 0;
    std::uint32_t targetY = 0;
    float actionInputTime = 0.0f;
    float actionInputElapsed = 0.0f;
};
} // namespace Client::Scenes
