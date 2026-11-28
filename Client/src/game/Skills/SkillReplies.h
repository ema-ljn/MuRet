#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Protocol::Skills
{
struct MagicAttack;
struct DurationMagic;
struct MagicCancel;
struct ActorState;
struct ActorEffect;
struct SkillTarget;
struct SkillList;
struct SummonLife;
struct LevelUp;
} // namespace Client::Protocol::Skills

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The incoming skills: what the handlers of 0x4e2b10 do with the world. Contract:
// Client/docs/research/06-render-escenas-y-ui.md, "Las skills que llegan". What MuRet lacks cuts with an error after
// what the original does first: the attack action of 0x4f4a10 (milestone 3), the flags of 0x4e6380 (milestone 1), the
// 0x518cf0 effects of types MuRet does not create and the joints of 0x530860 (CL11). The sounds stay in
// WorldRuntime::lastSoundId (CL10). The close rand() of +0x38e and +0x42e, and those of 0x4f2250 and 0x4f5290, in
// their order.
namespace Client::Skills
{

// 0x4cc530: the source's skill on the target (without the target, nothing). Except for 0x3b: the source's heading
// towards the target (if it is not the hero, nor skills 6 and 0xf, and the target is visible), the target in +0x3a4
// (and in +0x434 with 0x4c), +0x38d and the skill in +0x38e, protected. Then the skill's branch (06, the table of
// 0x4cc530). Without the source, what comes before the branch goes to slot 400 and the branch is dropped (Q-C1237).
[[nodiscard]] Core::Expected<void, std::string> ApplyMagicAttack(const Protocol::Skills::MagicAttack& attack,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4ce000: the duration skill. On the key's character: the skill in +0x38e, the heading (heading · 1/255 · 360) and
// the action, the attack one if it is not a player and the skill's if it is another player; then the cell (+0x39c,
// +0x39d), +0x381 at 1, +0x3a4 at 0xffff and +0x380 at 0. Without the character, all of that on slot 400 except the
// action, which is dropped: only the frame at 0 (Q-C1204).
[[nodiscard]] Core::Expected<void, std::string> ApplyDurationMagic(const Protocol::Skills::DurationMagic& magic,
                                                                   const Scenes::WorldEntryResources& resources);

// 0x4cbe90: the end of a duration skill: its flag (0x4e6910), with 0x439d40 first for 0xc9 and 0xca. On slot 400 if
// the key is absent.
[[nodiscard]] Core::Expected<void, std::string> ApplyMagicCancel(const Protocol::Skills::MagicCancel& cancel,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4cbe00: with type 1 and the mask not fully set in +0x78, 0x4e6380 (milestone 1, cuts); with type 1 and the mask
// set, +0x7c stays at 0x200000 if it had it and at 0 if not; with another type and the mask set, 0x4e6910.
[[nodiscard]] Core::Expected<void, std::string> ApplyActorState(const Protocol::Skills::ActorState& state,
                                                                const Scenes::WorldEntryResources& resources);

// 0x4cc290: without the target, nothing. Otherwise the source in 0x5767964, the skill in +0x42e (protected), the
// target in +0x3a4 and its bit 15 in +0x38d, and with skills 1 to 0xd, the attack action (0x4f4a10, milestone 3), the
// frame at 0 and +0x381 at 1. Without the source, all of that on slot 400 except the attack action, which is dropped
// (Q-C1237).
[[nodiscard]] Core::Expected<void, std::string> ApplySkillTarget(const Protocol::Skills::SkillTarget& target,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4dbf50: with a key character that is a player (+0x88 at 1) and type 0x10, fifteen 0x60f joints (CL11, cuts), the
// 0x61e effect and sound 0x98.
[[nodiscard]] Core::Expected<void, std::string> ApplyActorEffect(const Protocol::Skills::ActorEffect& effect,
                                                                 const Scenes::WorldEntryResources& resources);

// 0x4c5070: the own record's skills, open (3848 rand() if it closes at 0); then it opens it again and counts +0x6a
// and +0x6b with Skill.bmd's +0x31 of each of the first 64 (60 rand() per close at 0); fixes the hero's +0x425 and
// closes it.
[[nodiscard]] Core::Expected<void, std::string> ApplySkillList(const Protocol::Skills::SkillList& list,
                                                               const Scenes::WorldEntryResources& resources);

// 0x4d30d0: own stats in one opening, the close and the hero's effect without filters.
[[nodiscard]] Core::Expected<void, std::string> ApplyLevelUp(const Protocol::Skills::LevelUp& reply,
                                                             const Scenes::WorldEntryResources& resources);

// 0x4e31e1: the summon's life, 0x5767960 (HudState::summonLife).
void ApplySummonLife(const Protocol::Skills::SummonLife& summon, const Scenes::WorldEntryResources& resources);
} // namespace Client::Skills
