#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The reader does not write an action's speed (Q-C87): 0x60dbe0 sets it by hand at the end, after Shadow
// (0x60f335..0x60f754), in keys per 40 ms step (Client/docs/research/06-render-escenas-y-ui.md, "Cuánto avanza la
// animación").
namespace Client::Data::Models
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
} // namespace Client::Data::Models
