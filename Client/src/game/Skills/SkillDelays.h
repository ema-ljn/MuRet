#pragma once

#include "Core/Expected.h"
#include "Tables/SkillTable.h"

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

// The skills' delay (+0x76c of the own record, by bar slot). Contract: 06, "La espera de las skills". Each access to
// the own record spends its 3848 rand() on closing at 0, and each one to a Skill.bmd entry, 60 (0x40f600), in the
// original's order.
namespace Client::Skills
{

struct SkillDelayContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // Skill.bmd (0x76bd1d8). nullptr: what reads it cuts.
    const Tables::SkillTable* skills = nullptr;
};

// 0x5521f0(skill, &requirement): WORD +0x2e of the Skill.bmd entry, opened and closed.
[[nodiscard]] Core::Expected<std::int32_t, std::string> SkillDelayRequirement(std::uint8_t skill,
                                                                              const SkillDelayContext& context);

// 0x552990(slot): the bar slot's skill (+0x6c + slot) and its delay (+0x28 of Skill.bmd); with a delay, if the
// record's has reached 0 and the requirement of 0x5521f0 does not exceed +0x36 + +0x20 (leadership), it sets it.
[[nodiscard]] Core::Expected<void, std::string> StartSkillDelay(std::uint8_t slot, const SkillDelayContext& context);

// 0x552c50(slot): the same, but says whether it can be used: not with the delay still running nor with the
// requirement unmet. With skill 0x4c it does not set it.
[[nodiscard]] Core::Expected<bool, std::string> TryStartSkillDelay(std::uint8_t slot, const SkillDelayContext& context);

// 0x552f10(elapsed): with the record open, lowers the running delays of the first +0x6a + 2 slots (up to 256), not
// below 0. 0x4eb620 calls it in scene 5 with the frame's milliseconds (GameScene::Step, at the start of each original
// frame).
[[nodiscard]] Core::Expected<void, std::string> CountDownSkillDelays(std::int32_t elapsed,
                                                                     const SkillDelayContext& context);
} // namespace Client::Skills
