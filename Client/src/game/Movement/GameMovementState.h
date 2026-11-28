#pragma once
#include <array>
#include <cstdint>
#include <optional>

// MoveHero's and 1C's durable globals: research 06, Caminar and Los cambios de mapa.
namespace Client::Movement
{
// 0x7758580, 0x7758584 and 0x7758588 (.bss 0): what 0x57cfc0 leaves before each hand's weapon skill
// (0x57d5ed..0x57d61b): 1, the hero's skill slot (+0x425) and the chosen one (0x6c8e30) if 0x558bb0 says it attacks,
// or −1. Without line of sight, 0, 0 and −1 (0x57d760). Read by 0x559890, 0x559510 and the four per-class functions
// (CL9 milestone 4).
struct WeaponSkillRequest
{
    std::uint32_t active = 0;
    std::uint32_t slot = 0;
    std::int32_t target = 0;
};

struct GameMovementState
{
    bool warpPending = false;
    std::int32_t gateRequestBlocked = 0;
    std::uint32_t lastTeleportMs = 0;
    std::int32_t arrivalYaw = 0;
    std::int32_t movementTarget = 0;
    std::int32_t facingMode = 0; // 0x78abb18, zero from the selection
    bool inputBlocked = false;
    bool movementBlocked = false;
    float heldSince = 0.0f;
    float heldElapsed = 0.0f;
    // SetCursorPos(x, y) of 0x56b77f (CL9 milestone 11): the request stays here, in Windows screen pixels, and moving
    // the cursor belongs to the platform (like milestone 6's 30 request). The last request wins; nobody consumes it
    // yet.
    std::optional<std::array<std::int32_t, 2>> cursorWarpRequest{};
    float selectedSkillRange = 0.0f;
    std::uint32_t targetX = 0;
    std::uint32_t targetY = 0;
    float actionInputTime = 0.0f;
    float actionInputElapsed = 0.0f;
    // 0x6c8ebc (.data −1): the slot of the character the hero goes to. Written by the click on the chosen NPC
    // (0x59dad3, CL9 milestone 2) and on an attackable character (0x59d3de, milestone 3); 0x5650c0 reads it with +0x379
    // at 3.
    std::int32_t attackTarget = -1;
    // 0x576791c (.bss 0): the GetTickCount of the last send that passed the 300 ms brake (PassesTheActionBrake).
    std::uint32_t lastActionTick = 0;
    // CL9 milestone 4. 0x77f3ba4 (.bss 0): the Wizard's Aqua Beam sets it to 0 (0x590afd). Who else writes and reads it
    // is not read yet.
    std::int32_t dword77f3ba4 = 0;
    // 0x6c8e50 (.data −1): the character MoveHero walks toward (0x59b94b, not ported). Set by the D window's follow
    // command (0x40a3e0, GameCommandsWindow) and set back to −1 by 0x57cfc0 on using a skill (0x57d294), the 43 that
    // empties the party (0x4e415e), the return to the selection (0x4c254b), the click (0x59b8f3, 0x59b972) and
    // 0x4d6977, these two not ported.
    std::int32_t followTarget = -1;
    WeaponSkillRequest weaponSkill{};
    // 0x77f3bad (.bss 0): the magic key counter (0x57cf90, Skill57cf90.h), from 1 to 0x32.
    std::uint8_t magicKeyCounter = 0;
};
} // namespace Client::Movement
