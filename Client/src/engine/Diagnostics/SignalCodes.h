#pragma once

#ifdef __linux__
#include <cstdint>

// En Linux una caída llega como una señal y no como una excepción de Windows. MuError.dmp guarda
// el código de la excepción (0x67fee5) y su texto (0x680260), así que el manejador de Linux busca
// el código de Windows que dice lo mismo (Q-C241). Solo existe en Linux: los si_code (FPE_INTDIV,
// ILL_PRVOPC...) son de POSIX.
namespace Client::Core::Crash
{
// El código de Windows de `signal` con su si_code `code`: SIGSEGV es un acceso inválido
// (0xC0000005), SIGBUS un error de página (0xC0000006), SIGILL una instrucción inválida o
// privilegiada, SIGFPE la división u operación de punto flotante que dice `code` (la división
// entera por 0 si no dice ninguna) y SIGTRAP un breakpoint o un paso. Un `code` que no es del
// kernel (raise, kill) cuenta solo la señal. 0 para una señal que el manejador no toma.
[[nodiscard]] std::uint32_t ExceptionCodeForSignal(int signal, int code);
} // namespace Client::Core::Crash
#endif
