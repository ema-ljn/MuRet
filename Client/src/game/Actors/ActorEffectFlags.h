#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// The effect flags of another character (+0x78 and +0x7c) that 0x12 and 0x45 bring: 0x4e6380 and 0x42ab40.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". *Static*.
namespace Client::Actors
{

// 0x4e6380(flags, character), bit by bit in the original's order: those that only set their bit in +0x78
// (1, 2, 4, 8, 0x80, 0x200, 0x4000, 0x8000 and 0x1000000), 0x800000 and 0x400000 (which turn off 0x40000 first),
// 0x10000000 in +0x7c, and 0x10000, 0x20000, 0x40000 (which turns off the first of 0x80000, 0x400000 and 0x800000
// first), 0x80000 (which turns off 0x40000) and 0x100000, which with types 0x204 to 0x208 overwrite the whole +0x7c
// instead of setting their bit. 0x400 and 0x10 turn off the 0x5ac/2 or /1 of `actor` in `effects` (0x518ac0) and
// create another with it as owner (0x518cf0, `CreateSkillFlare`; with the table full, none), always, and set their
// bit. 0x20 without its bit turns off the character's 0xc1/1 (0x518ac0) and creates 0xc1/1 and /2 with it as owner
// (World::CreateWizardModelEffect, which overwrites its +0xf4 light). 0x40 without its bit turns off the 0x628/0 of
// `actor`, creates another with it as owner (PutTheSkullOn) and requests sound 0x85 on it. 0x100 on a 0x20a without
// the bit (sound 0x84 stays in `lastSound`) turns off its 0x10f/0 (0x534f90) and creates the five 0x10f joints that
// turn around it; 0x1000 sets its bit and creates the three 0x10f/8 (both, 0x530860 of AuraJoints).
// With 0x2000000 and the published hero, 0x555190 adds Text[1828] with an empty name and type 1 before setting the
// bit, even if it was already set. It needs Text.bmd and the measurer; without the bit it does not consult them.
[[nodiscard]] Core::Expected<void, std::string> ApplyEffectFlags(Kernel::ActorState& actor, std::uint32_t flags,
                                                                 bool publishedHero,
                                                                 const Scenes::WorldEntryResources& resources);

// The two 0x518cf0 sites of an ice (for the error).
struct IceCalls
{
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

// A character's ice: 0x518ac0(0xc1, character, 1) and 0xc1/1 and /2 (z + 180) with it as owner, its position, its
// angles and its +0xf4 light, which each creation overwrites (World::CreateWizardModelEffect). Flag 0x20 (0x4e67bc)
// and the reply of 0x33 (0x4cd6bc) use it. With the table full, nothing and no rand().
[[nodiscard]] Core::Expected<void, std::string>
WrapInIce(Kernel::ActorState& actor, const Scenes::WorldEntryResources& resources, const IceCalls& calls);

// 0x42ab40(character, flags): only on map 30. With 0x80000, 0x400000 or 0x800000 (the first one present, in that
// order), +0x360 at 6, 7 or 8, freeing the layer first (0x414340) if it was at 0xc; otherwise, with 0x40000, +0x360
// at 0xc, freeing it first if it was at 6, 7 or 8; with none, it frees the layer if +0x360 is 6, 7, 8 or 0xc. Nobody
// reads what it returns (0x4c9250, 0x4c9396).
void ApplyCastleSiegeFlags(Kernel::ActorState& actor, std::uint32_t flags, std::int32_t map);
} // namespace Client::Actors
