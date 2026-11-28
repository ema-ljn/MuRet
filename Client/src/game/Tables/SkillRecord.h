#pragma once

#include "Tables/SkillTable.h"

#include <cstddef>

// Where each field the client reads sits in a Skill.bmd record.
namespace Client::Tables::SkillRecordLayout
{
inline constexpr std::size_t Level = 0x20;      // byte
inline constexpr std::size_t Damage = 0x21;     // byte
inline constexpr std::size_t Mana = 0x22;       // WORD
inline constexpr std::size_t Stamina = 0x24;    // WORD
inline constexpr std::size_t Range = 0x26;      // WORD
inline constexpr std::size_t Delay = 0x28;      // int
inline constexpr std::size_t Energy = 0x2c;     // byte
inline constexpr std::size_t Leadership = 0x2e; // WORD
inline constexpr std::size_t Attribute = 0x30;  // byte, 0xff without one
// 1: from the guild war (KillCount); 2: from another skill (Giver).
inline constexpr std::size_t Origin = 0x31;
inline constexpr std::size_t Giver = 0x32;
inline constexpr std::size_t KillCount = 0x33;
static_assert(KillCount < SkillRecordSize);
} // namespace Client::Tables::SkillRecordLayout
