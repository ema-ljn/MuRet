#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The reader does not write an action's speed (Q-C87): 0x60dbe0 sets it by hand at the end, after Shadow
// (0x60f335..0x60f754), in keys per 40 ms step (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto avanza la
// animación").
namespace Client::Tables
{
// One write of 0x60dbe0: the same speed for actions `first` to `last`, a loop or a single instruction of the original.
struct ActionSpeedWrite
{
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    float speed = 0.0f;
};

// The 181 writes, in the original's order and grouped as it writes them: 47 instructions. Later ones overwrite
// earlier ones.
[[nodiscard]] std::span<const ActionSpeedWrite> PlayerActionSpeeds();

// std::nullopt if no write touches `action`: 57 of the 205 keep whatever was on the heap (Q-C87, P-C14 a).
[[nodiscard]] std::optional<float> PlayerActionSpeed(std::size_t action);

// The ones that do not repeat and stay on the last key: 0x60dbe0 sets their +0 to 1 (0x60f54c..0x60f585), in this
// order.
inline constexpr std::array<std::uint8_t, 4> PlayerActionsWithoutRepeat = {195, 196, 193, 68};

// The Player.bmd actions more than one function names. Windows are inclusive.
// 0x512a30 and 0x513100 do not stand the character (0x4f2250) inside either window (0x512ba1..0x512bcd,
// 0x5130cc..0x513124); 0x4f9e61 holds the 0x20a's frame inside the first.
inline constexpr std::uint8_t FirstBusyAction = 0x22;
inline constexpr std::uint8_t LastBusyAction = 0x97;
inline constexpr std::uint8_t FirstEmoteAction = 0xc5;
inline constexpr std::uint8_t LastEmoteAction = 0xcc;
// Walking and running: the footsteps (0x508668) and the objects a walking hero pushes (0x438ad0, 0x52f550).
inline constexpr std::uint8_t FirstWalkAction = 0x0d;
inline constexpr std::uint8_t LastWalkAction = 0x21;
// Flying, and flying with a bow in the right hand: no footstep, and the wings' fast keys.
inline constexpr std::uint8_t FirstFlyingAction = 0x1e;
inline constexpr std::uint8_t SecondFlyingAction = 0x1f;
// Riding the horse: its footsteps and its dust.
inline constexpr std::uint8_t HorseWalkAction = 0x4b;
// A player hit back (0x4f53a0) or pushed by the wizard's skill (0x524b03).
inline constexpr std::uint8_t ShockAction = 0xc2;
// Sitting down: the sitting flag (0x4f2416) and Crywolf's altar (0x4e11cc).
inline constexpr std::uint8_t SittingAction = 0xca;
} // namespace Client::Tables
