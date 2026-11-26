#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace Client::Crt
{
class CrtRandom;
}

// What 0x612d90 writes with 0x612c80 (06-render-escenas-y-ui.md, "Los sonidos del modelo"). *Static*.
namespace Client::Sound
{
// 0x612c80: a model's ten sound WORDs (+0xaa..+0xbc of its BMD in [0x5769ad8]). 0x4f37c0 reads 0 and 1 (walking),
// 0x4f4a10 and 0x4f53a0 2 and 3 (attack and hit) and 0x4f5660 4 (death); −1 (0xffff) is no sound. Nothing else writes
// those WORDs.
using ModelSounds = std::array<std::int16_t, 10>;
// A missing model was written by nobody: in the original its ten WORDs stay 0, like all of 0x5769ad8's array after
// 0x60dbe0 (the REP STOSD of 0x60dc6e..0x60dc80).
using ModelSoundTable = std::map<std::uint16_t, ModelSounds>;

// 0x612d90(monster): the switch of 0x613127..0x613130 (0x615340's table, 0 to 0x56) calls 0x612c80 on model 0x152 +
// monster with its case's ten WORDs, except 0xc and 0xd, which both write 0x15e's and 0x15f's. Cases without
// 0x612c80 (0x26, 0x38, 0x3c, 0x3d, 0x4a, 0x4b, 0x4e to 0x55 and 0x57 on) write nothing. What precedes the switch
// (actions, scale and the skip of an already loaded model) belongs to the model load.
// MuRet does not do it (0x612dba..0x612dee, 08, P-C176): it always writes, and with the same values gives the same as
// the original.
void WriteMonsterSounds(std::int32_t monster, ModelSoundTable& table);

// Or nothing if nobody wrote it.
[[nodiscard]] std::optional<ModelSounds> WrittenSounds(const ModelSoundTable& table, std::uint16_t model);

// The ten WORDs the original reads: `sounds`' or, if nobody wrote them, 0x60dc80's ten zeros.
[[nodiscard]] ModelSounds SoundsOrZeros(const std::optional<ModelSounds>& sounds);

// The two-WORD sound (0x4f3efa..0x4f3f6c, walking, from 0; 0x4f51f1..0x4f5262, attack, and 0x4f553d..0x4f55ac, hit,
// from 2): with WORD `word` at −1, none and no rand(); otherwise rand() % 2 (signed, of a non-negative rand()) picks
// `word` or the next, signed, plus 5 with subtype 9 and plus 0xd2. Without the table, ten zero WORDs
// (SoundsOrZeros): the rand() and sound 0xd2 (0xd7 with subtype 9). *Static*.
[[nodiscard]] std::optional<std::int32_t> DrawModelSound(const std::optional<ModelSounds>& sounds, std::size_t word,
                                                         std::int32_t subtype, Crt::CrtRandom& random);
} // namespace Client::Sound
