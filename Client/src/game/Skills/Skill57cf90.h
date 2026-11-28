#pragma once

#include <cstdint>

namespace Client::Movement
{
struct GameMovementState;
} // namespace Client::Movement

// 0x57cf90(&key): the next magic's key, from counter 0x77f3bad (.bss 0): one more and, past 0x32, 1 (the one-byte INC
// and the unsigned JBE). Leaves it in `key` and returns it. The original, with a null pointer, returns 0 without
// touching the counter; here the reference is never null (the tail's 0x43, 0x59497a, calls it that way:
// HeroSkillsTail.cpp sends the 0 without calling it). The branches of 0x57cfc0 repeat it inline (0x581348, 0x589246,
// 0x58e6d9 and 0x58f822) with the hero's +0x1bd, which then goes to the 1E.
namespace Client::Skills
{

std::uint8_t NextMagicKey(Movement::GameMovementState& state, std::uint8_t& key);
} // namespace Client::Skills
