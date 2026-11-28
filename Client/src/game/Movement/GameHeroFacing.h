#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Movement
{
struct GameMovementState;

struct HeroFacingContext
{
    Kernel::ActorState& hero;
    GameMovementState& movement;
    // 0x59b357: IsOpen(0x40000), guild window 2. When open, mode 0x78abb18 at 1 does not stop the turn.
    bool secondGuildOpen = false;
    // 0x59b37a: 0x775857c, the mouse is on the UI.
    std::uint8_t mouseOnUi = 0;
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x59b3f2: half the world width (0x59b188).
    std::int32_t centerX = 320;
    // BL published by MoveHero's head prefix (0x59b25a..0x59b292).
    bool headTurningAllowed = true;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
};

// Idle branch 0x59b333..0x59b83a. Call it before walking; an arrival does not run this branch.
[[nodiscard]] Core::Expected<void, std::string> StepHeroFacing(const HeroFacingContext& context);
} // namespace Client::Movement
