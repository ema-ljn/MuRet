#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The original's fields and widths (02 § El manejador de caídas): the 0x67f840 dump is of an x86 process, so every
// address and register is 32 bits. No dynamic memory: it lives in a static buffer filled after the crash.
namespace Client::Diagnostics
{
// Header texts: lstrcpynA with 0x80 (0x67f947), 127 bytes and the 0.
inline constexpr std::size_t CrashTextSize = 0x80;
// lstrcpynA with 0x100 (0x67fe1c).
inline constexpr std::size_t CrashModulePathSize = 0x100;
// The walk stops at 48 (cmp ebp,0x30 at 0x67feae).
inline constexpr std::size_t MaxCrashModules = 0x30;
// At most 64 (cmp eax,0x40 at 0x682be2).
inline constexpr std::size_t MaxCrashFrames = 0x40;
// The dwords each frame keeps from the return address (cmp edi,0x10 at 0x6828d7).
inline constexpr std::size_t CrashFrameArguments = 0x10;
// The executable's VS_FIXEDFILEINFO: 13 dwords (0x67fb62) written whole to the file (0x67ff84).
inline constexpr std::size_t VersionInfoSize = 0x34;
// The x86 CONTEXT's FLOATING_SAVE_AREA and ExtendedRegisters (the FXSAVE image).
inline constexpr std::size_t X86FloatSaveSize = 0x70;
inline constexpr std::size_t X86ExtendedRegistersSize = 0x200;

// 0-terminated, with the rest 0.
using CrashText = std::array<char, CrashTextSize>;

// Like lstrcpynA: up to Size - 1 bytes and the 0, with the rest of the field 0. Allocates nothing.
template <std::size_t Size> void CopyCrashText(std::array<char, Size>& field, std::string_view text)
{
    field.fill('\0');
    std::copy_n(text.begin(), std::min(text.size(), Size - 1), field.begin());
}

// The MEMORYSTATUS of GlobalMemoryStatus (0x67f9b0), without dwLength, which is always 32. Each SIZE_T is 32 bits, as
// in the original.
struct MemoryStatus32
{
    std::uint32_t memoryLoad = 0;
    std::uint32_t totalPhysical = 0;
    std::uint32_t availablePhysical = 0;
    std::uint32_t totalPageFile = 0;
    std::uint32_t availablePageFile = 0;
    std::uint32_t totalVirtual = 0;
    std::uint32_t availableVirtual = 0;
};

// The x86 CONTEXT (0x2cc bytes), in winnt.h order. The original copies it whole from the exception (0x67fedb); the
// frame walk reads Ebp.
struct X86Context
{
    std::uint32_t contextFlags = 0;
    std::uint32_t dr0 = 0;
    std::uint32_t dr1 = 0;
    std::uint32_t dr2 = 0;
    std::uint32_t dr3 = 0;
    std::uint32_t dr6 = 0;
    std::uint32_t dr7 = 0;
    std::array<std::byte, X86FloatSaveSize> floatSave{};
    std::uint32_t segGs = 0;
    std::uint32_t segFs = 0;
    std::uint32_t segEs = 0;
    std::uint32_t segDs = 0;
    std::uint32_t edi = 0;
    std::uint32_t esi = 0;
    std::uint32_t ebx = 0;
    std::uint32_t edx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t eax = 0;
    std::uint32_t ebp = 0;
    std::uint32_t eip = 0;
    std::uint32_t segCs = 0;
    std::uint32_t eflags = 0;
    std::uint32_t esp = 0;
    std::uint32_t segSs = 0;
    std::array<std::byte, X86ExtendedRegistersSize> extendedRegisters{};
};

// As 0x67fe6a copies it from a MODULEENTRY32.
struct CrashModule
{
    CrashText name{};
    std::array<char, CrashModulePathSize> path{};
    std::uint32_t base = 0;
    std::uint32_t size = 0;
};

// An EBP chain frame (0x682890): where it is, where it returns and the dwords that follow.
struct CrashFrame
{
    std::uint32_t framePointer = 0;
    std::uint32_t returnAddress = 0;
    std::array<std::uint32_t, CrashFrameArguments> arguments{};
};

struct CrashReport
{
    std::uint32_t time = 0;
    CrashText system{};
    CrashText processor{};
    MemoryStatus32 memory;
    std::uint32_t processId = 0;
    CrashText executable{};
    std::uint32_t executableCrc = 0;
    std::uint32_t threadCount = 0;
    bool hasVersionInfo = false;
    std::array<std::byte, VersionInfoSize> versionInfo{};
    // How many of `modules` go to the file.
    std::uint32_t moduleCount = 0;
    std::array<CrashModule, MaxCrashModules> modules{};
    X86Context context;
    std::uint32_t exceptionCode = 0;
    std::uint32_t exceptionAddress = 0;
    CrashText exceptionText{};
    // How many of `frames` go to the file.
    std::uint32_t frameCount = 0;
    std::array<CrashFrame, MaxCrashFrames> frames{};
};
} // namespace Client::Diagnostics
