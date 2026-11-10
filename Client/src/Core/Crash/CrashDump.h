#pragma once

#include "Core/Crash/CrashReport.h"

#include <cstddef>
#include <span>

// El archivo MuError.dmp de 0x67f840 (02 § El manejador de caídas), byte a byte: la cabecera de
// 0x516 bytes, el VS_FIXEDFILEINFO si lo hay, los módulos y los frames, en little-endian y con los
// campos pegados, sin relleno (la cabecera tiene dwords en offsets impares).
namespace Client::Core::Crash
{
// La cabecera: el tamaño que escribe en su campo 0x005 (0x67f8f1).
inline constexpr std::size_t CrashHeaderSize = 0x516;
// Un módulo (0x67ffa2) y un frame (0x680024).
inline constexpr std::size_t CrashModuleRecordSize = 0x188;
inline constexpr std::size_t CrashFrameRecordSize = 0x48;
// El archivo más grande: con la versión, 48 módulos y 64 frames.
inline constexpr std::size_t MaxCrashDumpSize =
    CrashHeaderSize + VersionInfoSize + MaxCrashModules * CrashModuleRecordSize + MaxCrashFrames * CrashFrameRecordSize;

// Lo que mide el archivo de `report`. Las cuentas de más de 48 módulos o 64 frames se toman como el
// tope, como lo que el original llega a juntar.
[[nodiscard]] std::size_t CrashDumpSize(const CrashReport& report);

// Escribe el archivo de `report` al principio de `output` y devuelve cuántos bytes escribió, o 0
// si no entra. No pide memoria: el manejador lo llama después de la caída, sobre un buffer
// estático de MaxCrashDumpSize.
[[nodiscard]] std::size_t WriteCrashDump(const CrashReport& report, std::span<std::byte> output);
} // namespace Client::Core::Crash
