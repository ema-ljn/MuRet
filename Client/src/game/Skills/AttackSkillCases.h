#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The skill cases of the attack step (0x4fd5f0; 08, P-C258): the head's switch (0x4fdeeb), the first with a target
// (0x504372) and actions 0x38 to 0x3c (0x5042ce, 0x52e740). 06, "El paso del ataque". Each case runs until it creates
// a model effect, a joint or a particle MuRet lacks (CL11), with the rand() before it; those that create what Effects
// already creates (the 0xfa earthquake, the 0x61e/1 decal, the arrow and the wizard effects of
// Effects/WizardModelEffects.h) run to the end.
namespace Client::Skills
{

// 0x4fd5f0's local light ([EBP − 0x30]): (1, 1, 1) at 0x4fd81e, unconditionally; particle 0x5ac of actions 0x4d, 0x4f
// and 0x5a past frame 5.5 sets it to (0.3f, 0.5, 1) (0x4fdb3f, Actors::CreateTheActionFlare). Read by the head's case
// 0x08 (0x500c32) and the target case 0x07 (0x50474e).
inline constexpr glm::vec3 BodyLight{1.0f, 1.0f, 1.0f};

// 0x4fdc17..0x4fdeeb and its cases, with `skill` the read of the head's +0x38e: 5, 8, 9, 0xa, 0xc..0xe, 0x1e..0x24,
// 0x26..0x2a, 0x2c, 0x30, 0x31, 0x35, 0x37, 0x38, 0x3f..0x41, 0x43..0x48, 0x4b and 0x4c (the byte table of 0x505530
// and the jump table of 0x5054b4). The others, like 0, do nothing (0x502aff). `actor` is c and o; `slot`, its pool
// slot and `light`, the local light of 0x4fd5f0. All cases continue at 0x502aff.
[[nodiscard]] Core::Expected<void, std::string> StepTheHeadSkill(Kernel::ActorState& actor, std::size_t slot,
                                                                 std::uint8_t skill,
                                                                 const Actors::CharacterStepContext& context,
                                                                 const glm::vec3& light = BodyLight);

// 0x504372 and its cases, with `skill` the read of +0x38e of 0x50432a and `victim` the target of +0x3a4: 1, 2, 4, 7,
// 0xb, 0x10, 0x11, 0x18, 0x1a..0x1c, 0x2d, 0x2e, 0x33, 0x34, 0x3c, 0x3d, 0x49 and 0x4a (the byte table of 0x5055c8 and
// the jump table of 0x505578). Case 3 and the others do nothing. All continue at 0x505069. Each case that reads the
// angles towards the target of 0x5042d3..0x504324 (o's, with the heading of 0x4e93f0 from o to the target, no rand())
// builds them.
[[nodiscard]] Core::Expected<void, std::string>
StepTheTargetSkill(Kernel::ActorState& actor, Kernel::ActorState& victim, std::size_t slot, std::uint8_t skill,
                   const Actors::CharacterStepContext& context, const glm::vec3& light = BodyLight);

// 0x5042ce: 0x52e740(0, target, target + 0x10, o + 0x1c), with o's actions 0x38 to 0x3c.
[[nodiscard]] Core::Expected<void, std::string> SparkTheTarget(const Kernel::ActorState& actor,
                                                               const Kernel::ActorState& victim, std::size_t slot,
                                                               const Actors::CharacterStepContext& context);
} // namespace Client::Skills
