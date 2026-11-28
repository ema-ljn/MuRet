#pragma once

#include <cstdint>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// 0x4e9c50(by, on): the action that 0x4ea030 sets on `on` by the hands of `by` (+0x2b8 and +0x2d8 other than −1):
// both 0x63, only the right 0x64, only the left 0x65 and none 0x62. Called by the knight's skill 0x4c (0x5896d2, with
// the hero in both), the branches 0x5817ce and 0x58eb7a of 0x57cfc0 and the incoming skills (SkillReplies, 0x4cc530).
// Here, only the action: the caller sets it with its skeleton.
namespace Client::Skills
{

[[nodiscard]] std::uint8_t ActionByTheHands(const Kernel::ActorState& by);
} // namespace Client::Skills
