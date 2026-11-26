#pragma once

#include "Diagnostics/CrashReport.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// What the Linux crash handler reads from /proc/self instead of the original's toolhelp snapshots
// (CreateToolhelp32Snapshot, 0x67f9c9 and 0x67fdb9; Q-C241). Pure functions on the text already read, with no new
// memory, so they are tested on any system.
namespace Client::Diagnostics
{
// Like Module32First/Module32Next: each mapped file (a path starting with '/') is one, with its name (after the last
// '/'), its path, the start of its first mapping and the size up to the end of its last. In file order, which is
// address order, up to 48; the rest is lost, as in the original. An address that does not fit in 32 bits keeps its low
// bits; a size that does not, 0xFFFFFFFF. Returns how many it filled.
[[nodiscard]] ENGINE_API std::size_t ModulesFromProcMaps(std::string_view maps,
                                                         std::span<CrashModule, MaxCrashModules> modules);

// Field 20 (num_threads) of /proc/self/stat: PROCESSENTRY32's cntThreads (0x67faf7). Empty if the text lacks that
// field.
[[nodiscard]] ENGINE_API std::optional<std::uint32_t> ThreadCountFromProcStat(std::string_view stat);
} // namespace Client::Diagnostics
