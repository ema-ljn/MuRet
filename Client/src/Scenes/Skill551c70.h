#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// 0x551c70(skill, nivel, nombre, &maná, &alcance, &stamina), CL9 hito 4, fase 2c: lo que pide una skill, de su entrada
// de Skill.bmd con el acceso abierto y cerrado (60 rand() si su cuenta vuelve a 0, 0x40f600). Sin el nombre (el strcpy
// del tercer argumento), que las ramas de las clases no piden. La llaman 0x559890, 0x561f60, las ramas de las clases de
// 0x57cfc0 (0x57d803, 0x580911, 0x58190a, 0x58984c y 0x589866) y los tooltips (que tienen su copia en
// ItemTooltipParts). El nivel no lo lee.
namespace Client::Scenes
{
struct SkillDelayContext;

// Lo que deja en sus punteros, en int: el maná (+0x22, WORD), el alcance (+0x26, byte) y la stamina (+0x24, WORD).
struct SkillCosts
{
    std::int32_t mana = 0;
    std::int32_t range = 0;
    std::int32_t stamina = 0;
};

[[nodiscard]] Core::Expected<SkillCosts, std::string> SkillCostsOf(std::uint8_t skill,
                                                                   const SkillDelayContext& context);
} // namespace Client::Scenes
