#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// 0x551f40(skill, &requirement): the energy a skill asks for, from its Skill.bmd entry with the access opened and
// closed (60 rand() if its count returns to 0, 0x40f600). With byte +0x2c at 0, 0; otherwise (+0x20 · +0x2c · 4) /
// 100 + 20, with the signed division by 0x51eb851f (*static*). Called by the class branches of 0x57cfc0 (0x57e075,
// 0x582310, 0x589885 and 0x593feb), against +0x30 + +0x1e of the own record.
namespace Client::Skills
{
struct SkillDelayContext;

[[nodiscard]] Core::Expected<std::int32_t, std::string> SkillEnergyRequirement(std::uint8_t skill,
                                                                               const SkillDelayContext& context);
} // namespace Client::Skills
