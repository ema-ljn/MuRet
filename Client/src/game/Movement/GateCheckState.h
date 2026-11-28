#pragma once

#include <cstdint>

// The gate decision's input (06 § Los cambios de mapa). The defaults only initialize the DTO, they are not a snapshot
// of the original; the caller fills every field of the step.
namespace Client::Movement
{
struct GateCheckState
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t map = 0;
    std::uint16_t level = 0;
    std::uint8_t characterClass = 0;
    // Item types of +0xdac/+0xd64, not CharacterAppearance models; -1 is empty.
    std::int16_t mount = -1;
    std::int16_t wings = -1;
    // WORD at 0x07879f80: the alternate mount that only the Atlans restriction checks.
    std::int16_t alternateMount = -1;
    // Signed DWORD at 0x07879fbc > 0 (TEST/JLE at 0x59a22f..0x59a231), not just nonzero.
    bool alternateMountActive = false;
    // Already decremented by MoveHero if it was positive; GateCheck requires exactly zero.
    std::int32_t warpCounter = 0;
    // Unsigned subtraction of GetTickCount and the last 1C: keeps the 32-bit wrap.
    std::uint32_t elapsedMs = 0;
    bool heroGateBlocked = false; // +0x399 nonzero.
    bool requestBlocked = false;  // 0x576794c nonzero.
    bool warpPending = false;     // 0x77f3bb0 nonzero.
};
} // namespace Client::Movement
