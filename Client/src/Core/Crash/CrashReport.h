#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Lo que junta el manejador de caídas para MuError.dmp, con los campos y los anchos del original
// (02 § El manejador de caídas): el volcado de 0x67f840 es de un proceso x86, así que cada
// dirección y cada registro mide 32 bits. Lo arma Crash::CrashHandler al caer y lo escribe
// WriteCrashDump. Es un agregado sin memoria dinámica: vive en un buffer estático que se llena
// después de la caída sin pedir memoria.
namespace Client::Core::Crash
{
// Los textos de la cabecera: lstrcpynA con 0x80 (0x67f947), 127 bytes y el 0.
inline constexpr std::size_t CrashTextSize = 0x80;
// La ruta de un módulo: lstrcpynA con 0x100 (0x67fe1c).
inline constexpr std::size_t CrashModulePathSize = 0x100;
// Los módulos que se guardan: el recorrido corta en 48 (cmp ebp,0x30 en 0x67feae).
inline constexpr std::size_t MaxCrashModules = 0x30;
// Los frames: 64 como mucho (cmp eax,0x40 en 0x682be2).
inline constexpr std::size_t MaxCrashFrames = 0x40;
// Los dwords que guarda cada frame desde la dirección de vuelta (cmp edi,0x10 en 0x6828d7).
inline constexpr std::size_t CrashFrameArguments = 0x10;
// El VS_FIXEDFILEINFO del ejecutable: 13 dwords (0x67fb62) que van enteros al archivo (0x67ff84).
inline constexpr std::size_t VersionInfoSize = 0x34;
// El FLOATING_SAVE_AREA y los ExtendedRegisters (la imagen de FXSAVE) del CONTEXT de x86.
inline constexpr std::size_t X86FloatSaveSize = 0x70;
inline constexpr std::size_t X86ExtendedRegistersSize = 0x200;

// Un texto de la cabecera, terminado en 0 y con el resto en 0.
using CrashText = std::array<char, CrashTextSize>;

// Copia `text` a un campo de largo fijo como lstrcpynA: hasta Size - 1 bytes y el 0, con el resto
// del campo en 0. No pide memoria.
template <std::size_t Size> void CopyCrashText(std::array<char, Size>& field, std::string_view text)
{
    field.fill('\0');
    std::copy_n(text.begin(), std::min(text.size(), Size - 1), field.begin());
}

// El MEMORYSTATUS de GlobalMemoryStatus (0x67f9b0), sin dwLength, que es siempre 32. Cada SIZE_T
// es de 32 bits, como en el original.
struct MemoryStatus32
{
    std::uint32_t memoryLoad = 0;
    std::uint32_t totalPhysical = 0;
    std::uint32_t availablePhysical = 0;
    std::uint32_t totalPageFile = 0;
    std::uint32_t availablePageFile = 0;
    std::uint32_t totalVirtual = 0;
    std::uint32_t availableVirtual = 0;
};

// El CONTEXT de x86 (0x2cc bytes), en el orden de winnt.h. El original lo copia entero desde la
// excepción (0x67fedb); el walk de los frames lee Ebp.
struct X86Context
{
    std::uint32_t contextFlags = 0;
    std::uint32_t dr0 = 0;
    std::uint32_t dr1 = 0;
    std::uint32_t dr2 = 0;
    std::uint32_t dr3 = 0;
    std::uint32_t dr6 = 0;
    std::uint32_t dr7 = 0;
    std::array<std::byte, X86FloatSaveSize> floatSave{};
    std::uint32_t segGs = 0;
    std::uint32_t segFs = 0;
    std::uint32_t segEs = 0;
    std::uint32_t segDs = 0;
    std::uint32_t edi = 0;
    std::uint32_t esi = 0;
    std::uint32_t ebx = 0;
    std::uint32_t edx = 0;
    std::uint32_t ecx = 0;
    std::uint32_t eax = 0;
    std::uint32_t ebp = 0;
    std::uint32_t eip = 0;
    std::uint32_t segCs = 0;
    std::uint32_t eflags = 0;
    std::uint32_t esp = 0;
    std::uint32_t segSs = 0;
    std::array<std::byte, X86ExtendedRegistersSize> extendedRegisters{};
};

// Un módulo, como lo copia 0x67fe6a de un MODULEENTRY32.
struct CrashModule
{
    CrashText name{};
    std::array<char, CrashModulePathSize> path{};
    std::uint32_t base = 0;
    std::uint32_t size = 0;
};

// Un frame de la cadena de EBP (0x682890): dónde está, adónde vuelve y los dwords que siguen.
struct CrashFrame
{
    std::uint32_t framePointer = 0;
    std::uint32_t returnAddress = 0;
    std::array<std::uint32_t, CrashFrameArguments> arguments{};
};

struct CrashReport
{
    std::uint32_t time = 0;
    CrashText system{};
    CrashText processor{};
    MemoryStatus32 memory;
    std::uint32_t processId = 0;
    CrashText executable{};
    std::uint32_t executableCrc = 0;
    std::uint32_t threadCount = 0;
    bool hasVersionInfo = false;
    std::array<std::byte, VersionInfoSize> versionInfo{};
    // Cuántos de `modules` van al archivo.
    std::uint32_t moduleCount = 0;
    std::array<CrashModule, MaxCrashModules> modules{};
    X86Context context;
    std::uint32_t exceptionCode = 0;
    std::uint32_t exceptionAddress = 0;
    CrashText exceptionText{};
    // Cuántos de `frames` van al archivo.
    std::uint32_t frameCount = 0;
    std::array<CrashFrame, MaxCrashFrames> frames{};
};
} // namespace Client::Core::Crash
