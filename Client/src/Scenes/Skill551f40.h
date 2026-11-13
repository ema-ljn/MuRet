#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>
#include <string_view>

// CL9 hito 4, fase 2c: la energía que pide una skill (0x551f40), que miran las ramas del elfo, de las clases 1, 3 y 4,
// del Wizard y de las 0x43 a 0x48 de 0x57cfc0 (0x57e075, 0x582310, 0x589885 y 0x593feb) contra +0x1e + +0x30 del
// registro propio.
namespace Client::Scenes
{
// 0x551f40(skill, &requisito): con el byte +0x2c de la entrada de Skill.bmd en 0, 0; si no, (+0x20 · +0x2c · 4) / 100
// + 20 (los dos bytes sin signo, la división con signo de 0x5520f4). Abre y cierra la entrada (60 rand() si su cuenta
// vuelve a 0). Sin Skill.bmd, corta con `where`.
[[nodiscard]] Core::Expected<std::int32_t, std::string>
SkillEnergyOf(std::uint8_t skill, const HeroSkillContext& context, std::string_view where);
} // namespace Client::Scenes
