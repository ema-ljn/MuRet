#pragma once

#include "Core/Expected.h"
#include "Effects/ModelEffects.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
struct ModelEffectArguments;
class WorldState;
} // namespace Client::Kernel

// The callers of CL11's "switch" batch of model effects (Effects/BlastModelEffects) from the character steps: 0x43a7a0
// (Kalima), 0x4f5b70 (the monsters), 0x4fd5f0 (the skills) and 0x4f97b0.
namespace Client::Combat
{

// +0x108 with a character: its type (+0x2), angles (+0x1c), position (+0x10), the alive byte (+0) and +0x16c, which
// the steps read live.
[[nodiscard]] Effects::ModelEffectOwner BlastOwnerOf(const Kernel::ActorState& actor);

// 0x518cf0(`arguments`) for a type of the batch, with `owner` (or nobody) in +0x108 and its +0x8c and +0x8e
// (ModelEffect::itemType and itemLevel). What the creation asks for comes from `context`: the own record and the
// world's hero, 0x517a20's targets (Actors/SkillHits, with context.send), the joints (context.joints, with the effect
// as owner when the creation makes itself the owner) and the sound. The effect's slot, or nothing with the table full;
// what the creation lacks cuts.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffect(const Kernel::ModelEffectArguments& arguments, Kernel::ActorState* owner,
                  const Actors::CharacterStepContext& context);

// 0x518cf0 outside the character step (the viewport packets, like the transformed one's 0xec arriving at 0x4c965c, and
// the skill replies, like 0x3b's 0x112 at 0x4cddf5): the same owner and the global table's joints
// (WorldState::DrawTail), without the own record, the targets or the sound; if the creation asks for them, it cuts.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateBlastEffectOutsideTheStep(const Kernel::ModelEffectArguments& arguments, Kernel::ActorState* owner,
                                Kernel::WorldState& world, Crt::CrtRandom& random);
} // namespace Client::Combat
