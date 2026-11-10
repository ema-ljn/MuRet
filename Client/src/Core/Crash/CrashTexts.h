#pragma once

#include "Core/Crash/CrashReport.h"

#include <cstdint>
#include <string>
#include <string_view>

// Los tres textos de la cabecera de MuError.dmp y la línea del log, como los arma el original
// (02 § El manejador de caídas): el sistema (0x683ed0), el procesador (0x684730) y la excepción
// (0x680260). Son funciones puras, sobre lo que el manejador leyó del sistema, y no piden memoria
// salvo CrashLogLine, que corre al instalar. Cada texto se corta en 127 bytes y el 0, como el
// lstrcpynA que lo copia a la cabecera.
namespace Client::Core::Crash
{
// Lo que 0x683ed0 lee de GetVersionExA, y el ProductType del registro que mira solo con 5.0.
struct OsVersion
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;
    std::uint32_t platformId = 0;
    // szCSDVersion, el service pack.
    std::string_view servicePack{};
    // HKLM\SYSTEM\CurrentControlSet\Control\ProductOptions, ProductType.
    std::string_view productType{};
};

// Lo que 0x684730 lee con cpuid: el fabricante de la hoja 0 (EBX, EDX y ECX) y la firma de la 1
// (EAX).
struct CpuId
{
    std::uint32_t vendorEbx = 0;
    std::uint32_t vendorEdx = 0;
    std::uint32_t vendorEcx = 0;
    std::uint32_t signature = 0;
};

// El texto del sistema de 0x683ed0: el nombre por la versión ("Windows XP", o "Unknown 6.2"), el
// build y el service pack entre paréntesis, pegado: "Windows XP Build 2600(Service Pack 3)".
[[nodiscard]] CrashText SystemText(const OsVersion& version);

// El de MuRet en Linux, que no tiene GetVersionExA: el sistema y la versión de uname,
// "Linux 6.8.0" (Q-C241).
[[nodiscard]] CrashText PosixSystemText(std::string_view systemName, std::string_view release);

// El texto del procesador de 0x684730: el fabricante, " - ", el modelo de su tabla y la velocidad,
// "GenuineIntel - Pentium 4 CPU 3.19GHz".
[[nodiscard]] CrashText ProcessorText(const CpuId& cpu, std::int64_t megahertz);

// Los MHz de 0x684ed0: los ciclos del TSC sobre el tiempo de la muestra, truncados a Hz y
// divididos por un millón. 0 si la muestra no tiene tiempo.
[[nodiscard]] std::int64_t MegahertzFromSample(std::uint64_t cycles, std::int64_t ticks, std::int64_t ticksPerSecond);

// El texto de la excepción de 0x680260, por su código. Un acceso inválido suma
// " : reading inaccessible 0x%08X" o " : writing ...", según ExceptionInformation[0] y con la
// dirección de ExceptionInformation[1]. MuRet escribe la dirección entera: más de 8 dígitos si no
// entra en 32 bits (Q-C241).
[[nodiscard]] CrashText ExceptionText(std::uint32_t code, std::uint64_t accessKind, std::uint64_t accessAddress);

// El mensaje que 0x4b9a70 deja en MuError.log cuando el volcado salió bien (0x6c80b4), sin el
// "\r\n".
inline constexpr std::string_view DumpSavedMessage = "Save DumpFile complete - MuError.dmp";

// La línea de MuRet: el mensaje con el prefijo del Log, como la escribiría Log::Error, sin el fin de
// línea, que pone quien la escribe.
[[nodiscard]] std::string CrashLogLine();
} // namespace Client::Core::Crash
