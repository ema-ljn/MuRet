#pragma once

#include "EngineApi.h"

#ifdef __linux__
#include <cstdint>

// MuError.dmp keeps the exception code (0x67fee5) and its text (0x680260), so the Linux handler looks up the Windows
// code that means the same (Q-C241). Linux only: the si_code values (FPE_INTDIV, ILL_PRVOPC...) are POSIX.
namespace Client::Diagnostics
{
// SIGSEGV is an access violation (0xC0000005), SIGBUS a page error (0xC0000006), SIGILL an invalid or privileged
// instruction, SIGFPE the division or floating-point operation `code` names (integer division by 0 if none) and
// SIGTRAP a breakpoint or a step. A `code` not from the kernel (raise, kill) counts only the signal. 0 for a signal the
// handler does not take.
[[nodiscard]] ENGINE_API std::uint32_t ExceptionCodeForSignal(int signal, int code);
} // namespace Client::Diagnostics
#endif
