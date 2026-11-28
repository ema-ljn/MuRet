#pragma once

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Combat/ActorCombat.h"
#include "Skills/HeroSkills.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

// What the class branches of 0x57cfc0 and 0x55cbc0 do inline, with no address of their own, when sending (06, "Usar
// una skill"). All with UseTheHeroSkill's context.
namespace Client::Skills
{

// The "webzen" strings (0x6c91ac..0x6c9254 in 0x57cfc0 and 0x6c8fb0..0x6c8fc0 in 0x55cbc0): the inline strstr on the
// published hero's name (+0x1d9, with the strlen that runs on into +0x1e3). With the name, the branch does not send and
// continues with what comes after the send.
[[nodiscard]] bool HasTheSilentName(const Kernel::WorldState& world);

// The inline turning D3 (0x55d4ef.., like a step's SendMove): C1 06 D3 X Y (heading << 4), with the XOR32 and no C3
// (SendTheSkillPacket, no rand()), the heading of `angle` as DirectionFromAngle. An angle that cannot be written cuts
// with `where`.
[[nodiscard]] Core::Expected<void, std::string> SendTheTurn(std::uint8_t x, std::uint8_t y, float angle,
                                                            const HeroSkillContext& context, std::string_view where);

// What 0x4f5290, 0x4f3f80 and 0x4ea030 read from the branches: the world, the rand(), Player.bmd and Item.bmd
// (without the model library, like 0x5650c0's context from MoveHero).
[[nodiscard]] Combat::ActorCombatContext CombatContextOf(const HeroSkillContext& context);

// `actor`'s pool slot (0x743a16c), or the pool size if it is not one of its characters (0x558eb0 compares it with the
// published hero).
[[nodiscard]] std::size_t SlotOf(const Kernel::WorldState& world, const Kernel::ActorState& actor);

// The character's logical cell (+0x41c or +0x420) opened, read and closed: 4 rand() if its count returns to 0
// (0x40b570 and 0x412890 or 0x40b800, 0x412810 and 0x4e7e40 inline).
[[nodiscard]] std::uint8_t ReadTheSkillCell(std::uint8_t& depth, std::uint8_t cell, Crt::CrtRandom& random);
} // namespace Client::Skills
