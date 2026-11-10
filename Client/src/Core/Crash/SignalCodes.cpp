#include "Core/Crash/SignalCodes.h"

#ifdef __linux__
#include <csignal>
#include <cstdint>

#include <signal.h>

namespace Client::Core::Crash
{
namespace
{
// Los códigos de la tabla de 0x680260 (CrashTexts.cpp).
constexpr std::uint32_t AccessViolation = 0xC0000005;
constexpr std::uint32_t InPageError = 0xC0000006;
constexpr std::uint32_t IllegalInstruction = 0xC000001D;
constexpr std::uint32_t PrivilegedInstruction = 0xC0000096;
constexpr std::uint32_t BoundsExceeded = 0xC000008C;
constexpr std::uint32_t FloatDivideByZero = 0xC000008E;
constexpr std::uint32_t FloatInexactResult = 0xC000008F;
constexpr std::uint32_t FloatInvalidOperation = 0xC0000090;
constexpr std::uint32_t FloatOverflow = 0xC0000091;
constexpr std::uint32_t FloatUnderflow = 0xC0000093;
constexpr std::uint32_t IntDivideByZero = 0xC0000094;
constexpr std::uint32_t IntOverflow = 0xC0000095;
constexpr std::uint32_t Breakpoint = 0x80000003;
constexpr std::uint32_t SingleStep = 0x80000004;

std::uint32_t IllegalInstructionCode(int code)
{
    return code == ILL_PRVOPC || code == ILL_PRVREG ? PrivilegedInstruction : IllegalInstruction;
}

std::uint32_t ArithmeticCode(int code)
{
    switch (code)
    {
    case FPE_INTOVF:
        return IntOverflow;
    case FPE_FLTDIV:
        return FloatDivideByZero;
    case FPE_FLTOVF:
        return FloatOverflow;
    case FPE_FLTUND:
        return FloatUnderflow;
    case FPE_FLTRES:
        return FloatInexactResult;
    case FPE_FLTINV:
        return FloatInvalidOperation;
    case FPE_FLTSUB:
        return BoundsExceeded;
    default:
        return IntDivideByZero;
    }
}
} // namespace

std::uint32_t ExceptionCodeForSignal(int signal, int code)
{
    // Los si_code del kernel son positivos; los de raise y kill (SI_USER, SI_TKILL), no.
    const int kernelCode = code > 0 ? code : 0;
    switch (signal)
    {
    case SIGSEGV:
        return AccessViolation;
    case SIGBUS:
        return InPageError;
    case SIGILL:
        return IllegalInstructionCode(kernelCode);
    case SIGFPE:
        return ArithmeticCode(kernelCode);
    case SIGTRAP:
        return kernelCode == TRAP_TRACE ? SingleStep : Breakpoint;
    default:
        return 0;
    }
}
} // namespace Client::Core::Crash
#endif
