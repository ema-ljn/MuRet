#include "Core/Crash/Registers64.h"

#include "Core/Crash/CrashReport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Client::Core::Crash::ToX86Context: los registros de x86-64 en el CONTEXT de x86 de MuError.dmp.
namespace
{
using Client::Core::Crash::FxsaveSize;
using Client::Core::Crash::Registers64;
using Client::Core::Crash::ToX86Context;
using Client::Core::Crash::X86Context;

// CONTEXT_i386 con CONTROL, INTEGER y SEGMENTS.
constexpr std::uint32_t BaseFlags = 0x00010007;

Registers64 SampleRegisters()
{
    Registers64 registers;
    registers.rax = 0x1111111100000001;
    registers.rbx = 0x2222222200000002;
    registers.rcx = 0x3333333300000003;
    registers.rdx = 0x4444444400000004;
    registers.rsi = 0x5555555500000005;
    registers.rdi = 0x6666666600000006;
    registers.rbp = 0x00007FFE0012FF30;
    registers.rsp = 0x00007FFE0012FF00;
    registers.rip = 0x00007FF7004B9C12;
    registers.eflags = 0x10246;
    registers.segCs = 0x33;
    registers.segDs = 0x2B;
    registers.segEs = 0x2B;
    registers.segFs = 0x53;
    registers.segGs = 0x2B;
    registers.segSs = 0x2B;
    return registers;
}
} // namespace

// Cada registro con sus 32 bits de abajo, en su campo del CONTEXT.
TEST(Registers64Test, KeepsTheLow32BitsOfEachRegister)
{
    const X86Context context = ToX86Context(SampleRegisters());

    EXPECT_EQ(context.contextFlags, BaseFlags);
    EXPECT_EQ(context.eax, 0x00000001u);
    EXPECT_EQ(context.ebx, 0x00000002u);
    EXPECT_EQ(context.ecx, 0x00000003u);
    EXPECT_EQ(context.edx, 0x00000004u);
    EXPECT_EQ(context.esi, 0x00000005u);
    EXPECT_EQ(context.edi, 0x00000006u);
    EXPECT_EQ(context.ebp, 0x0012FF30u);
    EXPECT_EQ(context.esp, 0x0012FF00u);
    EXPECT_EQ(context.eip, 0x004B9C12u);
    EXPECT_EQ(context.eflags, 0x10246u);
    EXPECT_EQ(context.segCs, 0x33u);
    EXPECT_EQ(context.segDs, 0x2Bu);
    EXPECT_EQ(context.segFs, 0x53u);
    EXPECT_EQ(context.segSs, 0x2Bu);
    // Sin los de depuración ni FXSAVE, esas partes quedan en 0.
    EXPECT_EQ(context.dr7, 0u);
    EXPECT_TRUE(std::all_of(context.extendedRegisters.begin(), context.extendedRegisters.end(),
                            [](std::byte value) { return value == std::byte{0}; }));
    EXPECT_TRUE(std::all_of(context.floatSave.begin(), context.floatSave.end(),
                            [](std::byte value) { return value == std::byte{0}; }));
}

// Con los registros de depuración (Windows), Dr0..Dr3, Dr6 y Dr7 y su bandera.
TEST(Registers64Test, CopiesTheDebugRegistersWhenThereAreSome)
{
    Registers64 registers = SampleRegisters();
    registers.hasDebugRegisters = true;
    registers.debugRegisters = {0x10, 0x20, 0x30, 0x40, 0xFFFF0FF0, 0x0000000100000400};

    const X86Context context = ToX86Context(registers);

    EXPECT_EQ(context.contextFlags, BaseFlags | 0x10u);
    EXPECT_EQ(context.dr0, 0x10u);
    EXPECT_EQ(context.dr3, 0x40u);
    EXPECT_EQ(context.dr6, 0xFFFF0FF0u);
    EXPECT_EQ(context.dr7, 0x400u);
}

// La imagen de FXSAVE va entera en ExtendedRegisters, que tiene el mismo formato.
TEST(Registers64Test, CopiesTheFxsaveImage)
{
    std::array<std::byte, FxsaveSize> fxsave{};
    fxsave[0] = std::byte{0x7F};
    fxsave[FxsaveSize - 1] = std::byte{0xEE};
    Registers64 registers = SampleRegisters();
    registers.fxsave = fxsave;

    const X86Context context = ToX86Context(registers);

    EXPECT_EQ(context.contextFlags, BaseFlags | 0x20u);
    EXPECT_EQ(context.extendedRegisters[0], std::byte{0x7F});
    EXPECT_EQ(context.extendedRegisters[FxsaveSize - 1], std::byte{0xEE});
}

// Una imagen corta no se copia: sería leer de más.
TEST(Registers64Test, IgnoresAShortFxsaveImage)
{
    const std::array<std::byte, 16> fxsave{std::byte{0x7F}};
    Registers64 registers = SampleRegisters();
    registers.fxsave = std::span<const std::byte>(fxsave);

    const X86Context context = ToX86Context(registers);

    EXPECT_EQ(context.contextFlags, BaseFlags);
    EXPECT_EQ(context.extendedRegisters[0], std::byte{0});
}
