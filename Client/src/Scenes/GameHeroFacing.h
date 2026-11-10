#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Scenes
{
struct ActorState;
struct GameMovementState;

struct HeroFacingContext
{
    ActorState& hero;
    GameMovementState& movement;
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    std::int32_t centerX = 320;
    // BL publicado por el prefijo de cabeza de MoveHero (59b25a..59b292).
    bool headTurningAllowed = true;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
};

// Rama quieta 59b333..59b83a. Invocar antes de caminar; una llegada no ejecuta esta rama.
[[nodiscard]] Core::Expected<void, std::string> StepHeroFacing(const HeroFacingContext& context);
} // namespace Client::Scenes
