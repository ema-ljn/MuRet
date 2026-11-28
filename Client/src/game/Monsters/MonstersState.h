#pragma once

#include "Math/HalfLifeMath.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// What the monsters own of the original's globals (ADR 0029).
namespace Client::Monsters
{
// What a monster's creation writes in the model table (0x5769ad8, 0xe0 bytes per model): the model's byte `offset` or,
// with `mesh`, byte +0 of that mesh (06, "El alta de un monstruo"). The monsters' drawing reads it, unported (08,
// P-C176).
struct ModelBytePatch
{
    std::uint16_t model = 0;
    std::optional<std::uint16_t> mesh{};
    std::uint16_t offset = 0;
    std::uint8_t value = 0;
};

struct MonstersState
{
    // 0x743748c, 0x74374bc and 0x74374ec: the matrices the +0x376 0x4d drawing copies (0x50c177): bones 23 and 14 of
    // its second model (the 0x18a) and 24 of the first (the 0x189), from +0x120. 0x4f5b70's attack reads them
    // (0x4f840e and 0x4f8b92).
    std::array<Math::BoneTransform, 3> phoenixBones{};
    // 0x7437488, guarded by bit 0 of 0x743a174 (.bss): the timeGetTime() of 0x6c88b0's last name message
    // (0x4fcd65..0x4fce15). A character's first step sets it to now − 5000; empty before. Nothing resets it.
    std::optional<std::uint32_t> namedLineTime{};
    // 0x743a190 (.bss, 0): with 1 the next name message is Text 1167 and with 0 it is 1168; it flips with each one.
    bool namedLineFirst = false;
    // What the creations wrote in the model table, in order.
    std::vector<ModelBytePatch> modelPatches{};
};
} // namespace Client::Monsters
