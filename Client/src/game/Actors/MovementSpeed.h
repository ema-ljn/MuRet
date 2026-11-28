#pragma once

#include <cstdint>

// 06-render-escenas-y-ui § Caminar: the whole speed function 0x508940, value and persistent state.
namespace Client::Actors
{
struct MovementSpeedInput
{
    std::uint16_t type = 0;
    std::uint16_t baseSpeed = 0;
    std::uint16_t item = 0xffff;
    std::uint16_t wings = 0xffff;
    std::uint8_t movementMode = 0;
    std::uint8_t itemVariant = 0;
    std::uint8_t safeZone = 0;
    std::uint32_t flags = 0;
    std::int32_t map = -1;
};

// A single instance across actors: the original's D0/D4, not per-actor state. The defaults are the DTO's; they do not
// claim how the native globals are initialized. D4 is compared as signed32.
struct MovementSpeedSharedState
{
    std::uint32_t itemLatch = 0;
    std::uint32_t itemCounterBits = 0;
};

// Run is the actor's only mutable byte and accepts 0..255. Stable inputs, distinct memories and state with a single
// owner during the call. Projects the return and the three states; does not replicate the original's ABI, argument
// slot, x87 status, faults or arbitrary aliases.
[[nodiscard]] float UpdateMovementSpeed(std::uint8_t& run, MovementSpeedSharedState& shared,
                                        const MovementSpeedInput& input);
} // namespace Client::Actors
