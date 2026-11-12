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
    // 0x59b357: IsOpen(0x40000), el gremio 2. Abierto, el modo 0x78abb18 en 1 no frena el giro.
    bool secondGuildOpen = false;
    // 0x59b37a: 0x775857c, el mouse es de la UI.
    std::uint8_t mouseOnUi = 0;
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    // 0x59b3f2: la mitad del ancho del mundo (0x59b188).
    std::int32_t centerX = 320;
    // BL publicado por el prefijo de cabeza de MoveHero (59b25a..59b292).
    bool headTurningAllowed = true;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
};

// Rama quieta 59b333..59b83a. Invocar antes de caminar; una llegada no ejecuta esta rama.
[[nodiscard]] Core::Expected<void, std::string> StepHeroFacing(const HeroFacingContext& context);
} // namespace Client::Scenes
