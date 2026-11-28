#pragma once

#include <cstdint>

// Globals of the siege controls and the clock object 0x6eeda0. They are not reset on a scene change.
namespace Client::CastleSiege
{
struct SiegeBattleUiState
{
    struct Clock
    {
        std::uint8_t blink = 0;    // +4
        std::int32_t x = 0;        // +8
        std::int32_t y = 0;        // +0xc
        std::uint32_t hours = 0;   // +0x10
        std::uint32_t minutes = 0; // +0x14
        float blinkTime = 0.0f;    // +0x18, the world's float clock
        std::uint32_t tick = 0;    // +0x1c, GetTickCount
    } clock{};
    std::uint16_t weaponKey = 0;   // 0x724594
    std::uint8_t weaponChoice = 0; // 0x724599
    std::uint32_t gateState = 0;   // 0x72459c
    std::uint16_t gateKey = 0;     // 0x7245a4
    std::uint8_t skillPressed = 0; // 0x7d536dc
};
} // namespace Client::CastleSiege
