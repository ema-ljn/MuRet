#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

// A character's state flags (+0x78 and +0x7c) that skills lower, and the effects and joints that go away with them.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las skills que llegan".
namespace Client::Kernel
{
class WorldState;
struct ActorState;
struct JointState;
} // namespace Client::Kernel

namespace Client::Actors
{
class IEffectHooks;

// 0x534f90(type, owner, subtype): the same among the 500 joints (type at +4, owner at +0x54 and subtype at +0xc),
// returning nothing. No rand().
void KillJointsOf(std::span<Kernel::JointState> joints, std::int32_t type, const void* owner, std::int32_t subtype);

// 0x4e6910(mask, character): lowers the flag of +0x78 (or of +0x7c) if it is set and, depending on which, turns off
// the effects and joints that go with it (0x518ac0, through `effects`, and 0x534f90), in `world`. With another mask,
// nothing. See the table in 06. Without `effects`, a flag with effects stops after lowering its bits.
[[nodiscard]] Core::Expected<void, std::string> RemoveActorFlag(std::uint32_t mask, Kernel::ActorState& actor,
                                                                Kernel::WorldState& world, const IEffectHooks* effects);

// 0x439d40: on a character that is not a 0x20a, with 0x100 in +0x78 it lowers it and sets +0x110 to 0x21; otherwise,
// with 8, it lowers it and sets 0x22.
void SettleMonsterFlags(Kernel::ActorState& actor);
} // namespace Client::Actors
