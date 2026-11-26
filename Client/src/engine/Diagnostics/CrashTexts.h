#pragma once

#include "Diagnostics/CrashReport.h"
#include "EngineApi.h"

#include <cstdint>
#include <string>
#include <string_view>

// As the original builds them (02 § El manejador de caídas): the system (0x683ed0), the processor (0x684730) and the
// exception (0x680260). Pure functions that allocate nothing except CrashLogLine, which runs on install. Each text is
// cut at 127 bytes and the 0, like the lstrcpynA that copies it to the header.
namespace Client::Diagnostics
{
// What 0x683ed0 reads from GetVersionExA, and the registry ProductType it checks only on 5.0.
struct OsVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;
    std::uint32_t platformId = 0;
    // szCSDVersion
    std::string_view servicePack{};
    // HKLM\SYSTEM\CurrentControlSet\Control\ProductOptions, ProductType.
    std::string_view productType{};
};

// What 0x684730 reads with cpuid: the vendor from leaf 0 (EBX, EDX and ECX) and the signature from leaf 1 (EAX).
struct CpuId
{
    std::uint32_t vendorEbx = 0;
    std::uint32_t vendorEdx = 0;
    std::uint32_t vendorEcx = 0;
    std::uint32_t signature = 0;
};

// 0x683ed0: the name by version ("Windows XP", or "Unknown 6.2"), the build and the service pack in parentheses, with
// no space: "Windows XP Build 2600(Service Pack 3)".
[[nodiscard]] ENGINE_API CrashText SystemText(const OsVersion& version);

// MuRet's on Linux, which has no GetVersionExA: uname's system and release, "Linux 6.8.0" (Q-C241).
[[nodiscard]] ENGINE_API CrashText PosixSystemText(std::string_view systemName, std::string_view release);

// 0x684730: the vendor, " - ", the model from its table and the speed, "GenuineIntel - Pentium 4 CPU 3.19GHz".
[[nodiscard]] ENGINE_API CrashText ProcessorText(const CpuId& cpu, std::int64_t megahertz);

// 0x684ed0: the TSC cycles over the sample time, truncated to Hz and divided by a million. 0 if the sample has no time.
[[nodiscard]] ENGINE_API std::int64_t MegahertzFromSample(std::uint64_t cycles, std::int64_t ticks,
                                                          std::int64_t ticksPerSecond);

// 0x680260, by code. An access violation adds " : reading inaccessible 0x%08X" or " : writing ...", by
// ExceptionInformation[0], with the address in ExceptionInformation[1]. MuRet writes the whole address: more than 8
// digits if it does not fit in 32 bits (Q-C241).
[[nodiscard]] ENGINE_API CrashText ExceptionText(std::uint32_t code, std::uint64_t accessKind,
                                                 std::uint64_t accessAddress);

// What 0x4b9a70 leaves in MuError.log when the dump succeeds (0x6c80b4), without the "\r\n".
inline constexpr std::string_view DumpSavedMessage = "Save DumpFile complete - MuError.dmp";

// The message with the Log prefix, as Log::Error would write it, without the line end, which the writer adds.
[[nodiscard]] ENGINE_API std::string CrashLogLine();
} // namespace Client::Diagnostics
