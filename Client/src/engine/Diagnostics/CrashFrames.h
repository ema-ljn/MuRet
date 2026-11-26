#pragma once

#include "Diagnostics/CrashReport.h"
#include "Diagnostics/IMemoryReader.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>

// The MuError.dmp frame list: the EBP chain the filter builds before writing (0x6829f0 and 0x682890; 02 § El manejador
// de caídas).
namespace Client::Diagnostics
{
// From `framePointer` (the CONTEXT's Ebp, +0xb4), for each readable frame: its address, the return address (what
// follows the saved previous frame) and up to 16 dwords from there, while readable. Follows the saved previous frame
// if readable, up to 64. `pointerSize` is 4 in the original, 8 in 64-bit MuRet. Each address keeps its low 32 bits,
// what fits in the file (Q-C241); the dwords start after the frame's two pointers. An unreadable return address stays
// 0: the original reads it unchecked (0x6828af). Allocates nothing.
[[nodiscard]] ENGINE_API std::size_t WalkCrashFrames(std::uint64_t framePointer, std::size_t pointerSize,
                                                     const IMemoryReader& memory,
                                                     std::span<CrashFrame, MaxCrashFrames> frames);
} // namespace Client::Diagnostics
