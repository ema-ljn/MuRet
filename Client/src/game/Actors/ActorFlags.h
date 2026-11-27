#pragma once

#include <cstdint>
#include <span>

// A character's state flags (+0x78 and +0x7c) that skills lower, and the effects and joints that go away with them.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las skills que llegan".
namespace Client::World
{
class ModelEffects;
}
namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct JointState;
} // namespace Client::Scenes

namespace Client::Actors
{

// 0x518ac0(type, owner, subtype): turns off the live model effects of that type (MOVSX of +2), that owner (+0x108)
// and that subtype (+4; −1, any), among the 200. Returns whether it turned any off. No rand(). MuRet's owner is the
// effect's ownerContext (*inferred*, Q-C1200: effects owned by a character keep it there).
bool KillModelEffectsOf(World::ModelEffects& effects, std::int16_t type, const void* owner, std::int32_t subtype);

// 0x534f90(type, owner, subtype): the same among the 500 joints (type at +4, owner at +0x54 and subtype at +0xc),
// returning nothing. No rand().
void KillJointsOf(std::span<Scenes::JointState> joints, std::int32_t type, const void* owner, std::int32_t subtype);

// 0x4e6910(mask, character): lowers the flag of +0x78 (or of +0x7c) if it is set and, depending on which, turns off
// the effects and joints that go with it (0x518ac0 and 0x534f90), in `world`. With another mask, nothing. See the
// table in 06.
void RemoveActorFlag(std::uint32_t mask, Kernel::ActorState& actor, Kernel::WorldState& world);

// 0x439d40: on a character that is not a 0x20a, with 0x100 in +0x78 it lowers it and sets +0x110 to 0x21; otherwise,
// with 8, it lowers it and sets 0x22.
void SettleMonsterFlags(Kernel::ActorState& actor);
} // namespace Client::Actors
