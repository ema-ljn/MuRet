#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Ordinary effects of the pure gate decision, for the caller to apply in its step.
namespace Client::Movement
{
struct GateWarning
{
    std::uint16_t textId = 0;
    std::optional<std::uint16_t> requiredLevel;

    friend bool operator==(const GateWarning&, const GateWarning&) = default;
};

struct GateDecision
{
    std::optional<std::uint8_t> request;
    // Without a wait, the sweep goes on: warnings can come before a request or another wait.
    std::vector<GateWarning> warnings;
    std::uint8_t waitSteps = 0;
    // 0x59abdc/0x59abfa ran the store of 0 to 0x77f3bb0, even if it was already zero. Apply it before the request, if
    // any: that request sets pending back to 1.
    bool clearWarpPending = false;
};
} // namespace Client::Movement
