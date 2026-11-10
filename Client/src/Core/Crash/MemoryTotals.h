#pragma once

#include "Core/Crash/CrashReport.h"

#include <cstdint>

// La memoria de la cabecera de MuError.dmp. El original la pide a GlobalMemoryStatus (0x67f9b0),
// que en un proceso de 32 bits da cada valor en 32 bits; MuRet la lee en 64 (GlobalMemoryStatusEx
// en Windows, sysinfo en Linux) y la pasa a esos 32 bits acá, con una función pura que se prueba
// en cualquier sistema.
namespace Client::Core::Crash
{
// Los seis totales de MEMORYSTATUS, en bytes y sin cortar.
struct MemoryTotals
{
    std::uint64_t totalPhysical = 0;
    std::uint64_t availablePhysical = 0;
    std::uint64_t totalPageFile = 0;
    std::uint64_t availablePageFile = 0;
    std::uint64_t totalVirtual = 0;
    std::uint64_t availableVirtual = 0;
};

// Los totales en 32 bits: el que no entra queda en 0xFFFFFFFF, el -1 con que GlobalMemoryStatus
// avisa que un valor no entra (Q-C242). `memoryLoad` va tal cual.
[[nodiscard]] MemoryStatus32 ToMemoryStatus32(const MemoryTotals& totals, std::uint32_t memoryLoad);

// El porcentaje en uso, de 0 a 100, como el dwMemoryLoad de Windows: lo que no está disponible
// sobre el total, truncado. 0 sin total. Lo usa Linux, que no lo da hecho.
[[nodiscard]] std::uint32_t MemoryLoadPercent(std::uint64_t total, std::uint64_t available);
} // namespace Client::Core::Crash
