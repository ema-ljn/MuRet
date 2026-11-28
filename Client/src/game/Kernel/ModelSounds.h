#pragma once

#include <array>
#include <cstdint>
#include <map>

// What 0x612d90 writes with 0x612c80 (06-render-escenas-y-ui.md, "Los sonidos del modelo"), which WorldRuntime holds
// and Sound reads. *Static*.
namespace Client::Kernel
{
// 0x612c80: a model's ten sound WORDs (+0xaa..+0xbc of its BMD in [0x5769ad8]). 0x4f37c0 reads 0 and 1 (walking),
// 0x4f4a10 and 0x4f53a0 2 and 3 (attack and hit) and 0x4f5660 4 (death); −1 (0xffff) is no sound. Nothing else writes
// those WORDs.
using ModelSounds = std::array<std::int16_t, 10>;
// A missing model was written by nobody: in the original its ten WORDs stay 0, like all of 0x5769ad8's array after
// 0x60dbe0 (the REP STOSD of 0x60dc6e..0x60dc80).
using ModelSoundTable = std::map<std::uint16_t, ModelSounds>;
} // namespace Client::Kernel
