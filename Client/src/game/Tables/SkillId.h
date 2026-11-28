#pragma once

#include <cstdint>

// The Skill.bmd rows (SkillTable.h) that several modules compare a skill with, named as the file names them.
namespace Client::Tables
{
inline constexpr std::uint8_t TripleShotSkill = 0x18;
inline constexpr std::uint8_t NovaSkill = 0x28;
inline constexpr std::uint8_t PenetrationSkill = 0x34;
inline constexpr std::uint8_t EarthshakeSkill = 0x3e;
// 0x43 to 0x48: the siege skills, from the stun to the dispel (their names are Korean in the file).
inline constexpr std::uint8_t StunSkill = 0x43;
inline constexpr std::uint8_t FirstSiegeSkill = StunSkill;
inline constexpr std::uint8_t LastSiegeSkill = 0x48;
// A skill byte that holds no skill.
inline constexpr std::uint8_t NoSkill = 0xff;
} // namespace Client::Tables
