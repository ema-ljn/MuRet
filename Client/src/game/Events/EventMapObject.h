#pragma once

#include <array>
#include <cstdint>

// The event object of 0x447d70 (DAT_00723c60, operator new(0x134)): Devil Square, Blood Castle or Chaos Castle.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los objetos de Devil Square y de Blood Castle".
namespace Client::Events
{
// The class, by the vtable that 0x447d70 (or 0x404d60) writes at +0x00. Its methods are called by the HUD (0x5a7320,
// CL8) and the packets (0x4e2b10, 0x4da730, 0x4da750, 0x4dac70, 0x4e7360, CL9.5).
enum class EventMapKind : std::uint8_t
{
    DevilSquare, // 0x6b8c1c
    BloodCastle, // 0x6b8c30
    ChaosCastle, // 0x6b8794, by 0x404d60
};

struct EventMapObject
{
    EventMapKind kind = EventMapKind::DevilSquare;
    // +0x04 and +0x08: neither 0x447d70 nor 0x404d60 writes them, and operator new does not initialize them (Q-C320).
    // MuRet leaves them at 0.
    std::int32_t field04 = 0;
    std::int32_t field08 = 0;
    std::int32_t field0c = 0;
    std::uint8_t field10 = 0;
    // +0x14..+0x20 at −1; 0x40ba20 resets them, with +0x10 at 0.
    std::array<std::int32_t, 4> field14{-1, -1, -1, -1};
    std::int32_t field24 = 0;
    std::int32_t field28 = 0;
    // +0x2c..+0x133: rep stos of 0x42 dwords at 0.
    std::array<std::int32_t, 0x42> field2c{};
};
} // namespace Client::Events
