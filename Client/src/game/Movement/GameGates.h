#pragma once

#include "Core/Expected.h"
#include "Tables/GateTable.h"
#include "Movement/GateDecision.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// 0x599ef0: Gate.bmd sweep with the logical accesses, effects and send in order (06, Pisar un gate).
namespace Client::Movement
{
struct GameGateContext
{
    Kernel::WorldState& world;
    const Tables::GateTable& gates;
    Crt::CrtRandom& random;
    std::uint32_t ticksMs = 0;
    std::function<void(std::uint8_t)> requestGate{};
    // 0x7879f80: only Atlans reads it, with cursorItemCount>0; zero while there is no producer.
    std::int16_t alternateMount = 0;
    // 0x59ad3b/0x59ad45: F3 30 and Data\Macro.txt when asking for a gate to destination 30/31, still inside the own
    // access.
    std::function<Core::Expected<void, std::string>()> saveOptionsAndMacros{};
};

// Applies the counter, pending and targets; the result keeps warnings for the future message area. It does not
// decrement the counter: MoveHero already did. Destination 30/31 saves options and macros after the send and the
// wait, before pending/targets.
[[nodiscard]] Core::Expected<Movement::GateDecision, std::string> CheckGameGates(const GameGateContext& context);
} // namespace Client::Movement
