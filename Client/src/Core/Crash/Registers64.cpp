#include "Core/Crash/Registers64.h"

#include "Core/Crash/CrashReport.h"

#include <algorithm>
#include <cstdint>

namespace Client::Core::Crash
{
namespace
{
// Los ContextFlags de x86 de winnt.h.
constexpr std::uint32_t ContextI386 = 0x00010000;
constexpr std::uint32_t ContextControl = 0x01;
constexpr std::uint32_t ContextInteger = 0x02;
constexpr std::uint32_t ContextSegments = 0x04;
constexpr std::uint32_t ContextDebugRegisters = 0x10;
constexpr std::uint32_t ContextExtendedRegisters = 0x20;

std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value);
}
} // namespace

X86Context ToX86Context(const Registers64& registers)
{
    X86Context context;
    context.contextFlags = ContextI386 | ContextControl | ContextInteger | ContextSegments;
    if (registers.hasDebugRegisters)
    {
        context.contextFlags |= ContextDebugRegisters;
        context.dr0 = Low32(registers.debugRegisters[0]);
        context.dr1 = Low32(registers.debugRegisters[1]);
        context.dr2 = Low32(registers.debugRegisters[2]);
        context.dr3 = Low32(registers.debugRegisters[3]);
        context.dr6 = Low32(registers.debugRegisters[4]);
        context.dr7 = Low32(registers.debugRegisters[5]);
    }
    context.segGs = registers.segGs;
    context.segFs = registers.segFs;
    context.segEs = registers.segEs;
    context.segDs = registers.segDs;
    context.edi = Low32(registers.rdi);
    context.esi = Low32(registers.rsi);
    context.ebx = Low32(registers.rbx);
    context.edx = Low32(registers.rdx);
    context.ecx = Low32(registers.rcx);
    context.eax = Low32(registers.rax);
    context.ebp = Low32(registers.rbp);
    context.eip = Low32(registers.rip);
    context.segCs = registers.segCs;
    context.eflags = registers.eflags;
    context.esp = Low32(registers.rsp);
    context.segSs = registers.segSs;
    if (registers.fxsave.size() >= context.extendedRegisters.size())
    {
        context.contextFlags |= ContextExtendedRegisters;
        std::copy_n(registers.fxsave.begin(), context.extendedRegisters.size(), context.extendedRegisters.begin());
    }
    return context;
}
} // namespace Client::Core::Crash
