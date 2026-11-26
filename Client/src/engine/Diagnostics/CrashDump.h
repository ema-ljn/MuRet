#pragma once

#include "Diagnostics/CrashReport.h"
#include "EngineApi.h"

#include <cstddef>
#include <span>

// MuError.dmp as 0x67f840 writes it (02 § El manejador de caídas): the 0x516-byte header, the VS_FIXEDFILEINFO if any,
// the modules and the frames, little-endian and packed with no padding (the header has dwords at odd offsets).
namespace Client::Diagnostics
{
// The size written in its field 0x005 (0x67f8f1).
inline constexpr std::size_t CrashHeaderSize = 0x516;
// A module (0x67ffa2) and a frame (0x680024).
inline constexpr std::size_t CrashModuleRecordSize = 0x188;
inline constexpr std::size_t CrashFrameRecordSize = 0x48;
inline constexpr std::size_t MaxCrashDumpSize =
    CrashHeaderSize + VersionInfoSize + MaxCrashModules * CrashModuleRecordSize + MaxCrashFrames * CrashFrameRecordSize;

// Counts over 48 modules or 64 frames are taken as the cap, the most the original collects.
[[nodiscard]] ENGINE_API std::size_t CrashDumpSize(const CrashReport& report);

// Returns the bytes written, or 0 if it does not fit. Allocates nothing: the handler calls it after the crash, on a
// static buffer of MaxCrashDumpSize.
[[nodiscard]] ENGINE_API std::size_t WriteCrashDump(const CrashReport& report, std::span<std::byte> output);
} // namespace Client::Diagnostics
