#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// 0x551f40(skill, &requisito), CL9 hito 4, fase 2c: la energía que pide una skill, de su entrada de Skill.bmd con el
// acceso abierto y cerrado (60 rand() si su cuenta vuelve a 0, 0x40f600). Con el byte +0x2c en 0, 0; si no,
// (+0x20 · +0x2c · 4) / 100 + 20, con la división con signo de 0x51eb851f (*estático*). La llaman las ramas de las
// clases de 0x57cfc0 (0x57e075, 0x582310, 0x589885 y 0x593feb), contra +0x30 + +0x1e del registro propio.
namespace Client::Scenes
{
struct SkillDelayContext;

[[nodiscard]] Core::Expected<std::int32_t, std::string> SkillEnergyRequirement(std::uint8_t skill,
                                                                               const SkillDelayContext& context);
} // namespace Client::Scenes
