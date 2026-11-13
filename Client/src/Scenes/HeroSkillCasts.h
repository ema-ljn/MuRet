#pragma once

#include "Core/Expected.h"
#include "Scenes/HeroSkills.h"

#include <cstdint>
#include <string>
#include <string_view>

// CL9 hito 4, fase 2c: lo que comparten las ramas de las clases de 0x57cfc0 y las funciones que mandan (06, "Usar una
// skill"). Todo con el contexto de UseTheHeroSkill; las ramas de cada clase lo llaman con el registro propio abierto,
// como el original.
namespace Client::Scenes
{
// Lo que 0x551c70(skill, 1, 0, &maná, 0, &stamina) deja en sus punteros: el WORD +0x22 y el WORD +0x24 de la entrada
// de Skill.bmd.
struct SkillCost
{
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
};

// 0x551c70 con el maná y la stamina: abre la entrada de la skill, lee y la cierra (60 rand() si su cuenta vuelve a 0).
// Sin Skill.bmd, corta con `where`.
[[nodiscard]] Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                                 std::string_view where);

// El camino sin maná de las skills (0x559b29, 0x55a398, 0x56205f, 0x57d821, 0x58093e, 0x581b86 y 0x589920): con el
// maná del registro propio (+0x24) debajo del de la skill, 0x558110(3) busca una poción de maná (0x1c04 a 0x1c06) en
// el inventario, sin rand(). Sin ella, nada: el que llama sigue con su camino de "no". Con ella, 0x5af520 mira las
// ventanas y el C1 26 la usa: son del hito 5, y corta con `where`, la dirección de su 0x5af520.
[[nodiscard]] Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where);
} // namespace Client::Scenes
