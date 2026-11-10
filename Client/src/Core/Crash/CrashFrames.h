#pragma once

#include "Core/Crash/CrashReport.h"
#include "Core/Crash/IMemoryReader.h"

#include <cstddef>
#include <cstdint>
#include <span>

// La lista de frames de MuError.dmp: la cadena de EBP que arma el filtro antes de escribir
// (0x6829f0 y 0x682890; 02 § El manejador de caídas).
namespace Client::Core::Crash
{
// Recorre la cadena desde `framePointer` (el Ebp del CONTEXT, +0xb4) y llena `frames`: por cada
// frame que se puede leer, su dirección, la de vuelta (lo que sigue al frame anterior guardado) y
// hasta 16 dwords desde ahí, de a uno mientras se puedan leer. Sigue con el frame anterior
// guardado si se puede leer, hasta 64. Devuelve cuántos llenó.
//
// `pointerSize` es lo que mide un puntero en el proceso: 4 en el original, 8 en MuRet de 64 bits.
// Cada dirección se guarda con sus 32 bits de abajo, que es lo que entra en el archivo (Q-C241), y
// los dwords empiezan después de los dos punteros del frame. La dirección de vuelta que no se puede
// leer queda en 0: el original la lee sin mirar (0x6828af). No pide memoria.
[[nodiscard]] std::size_t WalkCrashFrames(std::uint64_t framePointer, std::size_t pointerSize,
                                          const IMemoryReader& memory, std::span<CrashFrame, MaxCrashFrames> frames);
} // namespace Client::Core::Crash
