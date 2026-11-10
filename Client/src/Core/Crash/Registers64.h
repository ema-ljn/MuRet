#pragma once

#include "Core/Crash/CrashReport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Los registros de un proceso x86-64 al caer, como los dan el CONTEXT de Windows de 64 bits y el
// ucontext_t de Linux. MuRet es de 64 bits, y MuError.dmp guarda el CONTEXT de x86 de 0x2cc bytes
// que el original copia entero (0x67fedb): ToX86Context los pasa a ese formato (Q-C241).
namespace Client::Core::Crash
{
// Lo que mide la imagen de FXSAVE: el FltSave de Windows x64 y el fpregs de Linux.
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
    // Dr0, Dr1, Dr2, Dr3, Dr6 y Dr7, si el sistema los da (Windows sí, Linux no).
    bool hasDebugRegisters = false;
    std::array<std::uint64_t, 6> debugRegisters{};
    // La imagen de FXSAVE, de FxsaveSize bytes, o vacía si no la hay.
    std::span<const std::byte> fxsave{};
};

// El CONTEXT de x86: cada registro con sus 32 bits de abajo (eax de rax, eip de rip...), la imagen
// de FXSAVE en ExtendedRegisters, que tiene el mismo formato en los dos, y el FloatSave en 0
// (MuRet no lo arma desde FXSAVE). ContextFlags dice qué partes van llenas: CONTEXT_i386 con
// CONTROL, INTEGER y SEGMENTS, más DEBUG_REGISTERS y EXTENDED_REGISTERS si están.
[[nodiscard]] X86Context ToX86Context(const Registers64& registers);
} // namespace Client::Core::Crash
