#pragma once

#include "Diagnostics/CrashReport.h"
#include "EngineApi.h"

#include <cstdint>

// The original asks GlobalMemoryStatus (0x67f9b0), which in a 32-bit process gives each value in 32 bits; MuRet reads
// them in 64 (GlobalMemoryStatusEx on Windows, sysinfo on Linux) and narrows them here, in a pure function testable on
// any system.
namespace Client::Diagnostics
{
// In bytes, not narrowed.
struct MemoryTotals
{
    std::uint64_t totalPhysical = 0;
    std::uint64_t availablePhysical = 0;
    std::uint64_t totalPageFile = 0;
    std::uint64_t availablePageFile = 0;
    std::uint64_t totalVirtual = 0;
    std::uint64_t availableVirtual = 0;
};

// One that does not fit becomes 0xFFFFFFFF, the -1 GlobalMemoryStatus uses for a value that does not fit (Q-C242).
// `memoryLoad` goes as is.
[[nodiscard]] ENGINE_API MemoryStatus32 ToMemoryStatus32(const MemoryTotals& totals, std::uint32_t memoryLoad);

// Like Windows' dwMemoryLoad: what is not available over the total, truncated, 0 to 100. 0 with no total. For Linux,
// which does not provide it.
[[nodiscard]] ENGINE_API std::uint32_t MemoryLoadPercent(std::uint64_t total, std::uint64_t available);
} // namespace Client::Diagnostics
