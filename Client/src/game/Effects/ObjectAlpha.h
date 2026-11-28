#pragma once

#include <cstdint>

// Ordinary 0x4e9980: borrowed views of +0x170, +0x174 and +0x68; mode from +0x16d. PC53/RN profile, exact f32 operands
// and one R53 per operation, followed by f32 stores. The caller keeps RN and masked exceptions. Does not reproduce
// native flags/status, traps, concurrent changes or aliasing with constants/frame. Aliasing between views is allowed.
// Does not validate ranges or return a state: only modifies current and, if applicable, ceiling.
namespace Client::Effects
{
void StepObjectAlpha(const float& target, float& current, float& ceiling, std::uint8_t mode);
} // namespace Client::Effects
