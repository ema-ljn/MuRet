#pragma once

#include <array>
#include <cstdint>

// The Player.bmd bones more than one function names.
namespace Client::Tables
{
// The bones 0x60dbe0 writes at +0x54, +0x58 and +0x5c of the skeleton (0x60f301..0x60f32b): the head, and two that
// would be the feet (*inferred*).
inline constexpr std::int16_t PlayerHeadBone = 20;
inline constexpr std::array<std::int16_t, 2> PlayerFootBones = {6, 13};
// The bones the hands, the wings and the pet hang from (0x511290 and 0x50ba30).
inline constexpr std::int16_t PlayerRightHandBone = 33;
inline constexpr std::int16_t PlayerLeftHandBone = 42;
inline constexpr std::int16_t PlayerWingsBone = 47;
inline constexpr std::int16_t PlayerPetBone = 34;
} // namespace Client::Tables
