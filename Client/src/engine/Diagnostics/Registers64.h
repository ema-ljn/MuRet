#pragma once

#include "Diagnostics/CrashReport.h"
#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// As the 64-bit Windows CONTEXT and Linux's ucontext_t give them. MuRet is 64-bit, and MuError.dmp keeps the
// 0x2cc-byte x86 CONTEXT the original copies whole (0x67fedb): ToX86Context converts them to that format (Q-C241).
namespace Client::Diagnostics
{
// Windows x64's FltSave and Linux's fpregs.
inline constexpr std::size_t FxsaveSize = X86ExtendedRegistersSize;

struct Registers64
{
    std::uint64_t rax = 0;
    std::uint64_t rbx = 0;
    std::uint64_t rcx = 0;
    std::uint64_t rdx = 0;
    std::uint64_t rsi = 0;
    std::uint64_t rdi = 0;
    std::uint64_t rbp = 0;
    std::uint64_t rsp = 0;
    std::uint64_t rip = 0;
    std::uint32_t eflags = 0;
    std::uint16_t segCs = 0;
    std::uint16_t segDs = 0;
    std::uint16_t segEs = 0;
    std::uint16_t segFs = 0;
    std::uint16_t segGs = 0;
    std::uint16_t segSs = 0;
    // Dr0, Dr1, Dr2, Dr3, Dr6 and Dr7, if the system gives them (Windows does, Linux does not).
    bool hasDebugRegisters = false;
    std::array<std::uint64_t, 6> debugRegisters{};
    // FxsaveSize bytes, or empty if there is none.
    std::span<const std::byte> fxsave{};
};

// Each register with its low 32 bits (eax from rax, eip from rip...), the FXSAVE image in ExtendedRegisters, which has
// the same format in both, and FloatSave at 0 (MuRet does not build it from FXSAVE). ContextFlags: CONTEXT_i386 with
// CONTROL, INTEGER and SEGMENTS, plus DEBUG_REGISTERS and EXTENDED_REGISTERS when present.
[[nodiscard]] ENGINE_API X86Context ToX86Context(const Registers64& registers);
} // namespace Client::Diagnostics
