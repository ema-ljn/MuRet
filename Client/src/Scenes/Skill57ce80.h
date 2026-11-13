#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <string>

// CL9 hito 4, fase 2c: el blanco de una skill (0x57ce80), que llaman la cabeza de 0x57cfc0 (0x57d4c2, sin mirar el
// resultado), la rama del elfo (0x57e12b) y la del Wizard (0x58a118, que lo guarda en [ebp−0x3d]).
namespace Client::Scenes
{
struct ActorState;

// 0x57ce80(héroe): con un elegido (0x6c8e30), su celda (ftol de la posición · 0,01) en 0x77e1ba8/0x77e1bac y su
// posición en +0x3a8..+0x3b0 del héroe, y 1. Sin él, el punto del terreno bajo el mouse (0x5fc260(1) y 0x5fac90): si
// acierta, el punto en +0x3a8..+0x3b0 y su celda, y 1; si no, 0 sin tocar nada. Sin rand().
[[nodiscard]] Core::Expected<bool, std::string> AimTheHeroSkill(ActorState& hero, const HeroSkillContext& context);
} // namespace Client::Scenes
