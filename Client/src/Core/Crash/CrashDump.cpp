#include "Core/Crash/CrashDump.h"

#include "Core/Crash/CrashReport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Core::Crash
{
namespace
{
// La firma y la versión de la cabecera: 'W', 'D', 'M', 'P' y 2 (0x67f8c9..0x67f8e9).
constexpr std::array<std::byte, 4> Signature{std::byte{'W'}, std::byte{'D'}, std::byte{'M'}, std::byte{'P'}};
constexpr std::uint8_t Version = 2;
// El dwLength del MEMORYSTATUS (0x67f9a4).
constexpr std::uint32_t MemoryStatusLength = 0x20;

// Escribe en orden, sin relleno, en little-endian, sobre un buffer que ya se sabe que alcanza.
class DumpWriter
{
public:
    explicit DumpWriter(std::span<std::byte> output) : m_output(output) {}

    void Byte(std::uint8_t value)
    {
        m_output[m_position++] = std::byte{value};
    }

    void Dword(std::uint32_t value)
    {
        constexpr unsigned BitsPerDword = 32;
        constexpr unsigned BitsPerByte = 8;
        for (unsigned shift = 0; shift < BitsPerDword; shift += BitsPerByte)
        {
            Byte(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void Bytes(std::span<const std::byte> bytes)
    {
        std::copy(bytes.begin(), bytes.end(), m_output.begin() + static_cast<std::ptrdiff_t>(m_position));
        m_position += bytes.size();
    }

    // Un texto de largo fijo, tal cual: ya viene terminado en 0 y con el resto en 0.
    template <std::size_t Size> void Text(const std::array<char, Size>& text)
    {
        for (const char character : text)
        {
            Byte(static_cast<std::uint8_t>(character));
        }
    }

    [[nodiscard]] std::size_t Position() const
    {
        return m_position;
    }

private:
    std::span<std::byte> m_output;
    std::size_t m_position = 0;
};

std::uint32_t Capped(std::uint32_t count, std::size_t maximum)
{
    return std::min(count, static_cast<std::uint32_t>(maximum));
}

void WriteMemoryStatus(DumpWriter& writer, const MemoryStatus32& memory)
{
    writer.Dword(MemoryStatusLength);
    writer.Dword(memory.memoryLoad);
    writer.Dword(memory.totalPhysical);
    writer.Dword(memory.availablePhysical);
    writer.Dword(memory.totalPageFile);
    writer.Dword(memory.availablePageFile);
    writer.Dword(memory.totalVirtual);
    writer.Dword(memory.availableVirtual);
}

// El CONTEXT de x86 en el orden de winnt.h: 0x2cc bytes.
void WriteContext(DumpWriter& writer, const X86Context& context)
{
    writer.Dword(context.contextFlags);
    const std::array debug{context.dr0, context.dr1, context.dr2, context.dr3, context.dr6, context.dr7};
    for (const std::uint32_t value : debug)
    {
        writer.Dword(value);
    }
    writer.Bytes(context.floatSave);
    const std::array registers{context.segGs, context.segFs,  context.segEs, context.segDs, context.edi, context.esi,
                               context.ebx,   context.edx,    context.ecx,   context.eax,   context.ebp, context.eip,
                               context.segCs, context.eflags, context.esp,   context.segSs};
    for (const std::uint32_t value : registers)
    {
        writer.Dword(value);
    }
    writer.Bytes(context.extendedRegisters);
}

// La cabecera de 0x516 bytes, en el orden en que la arma 0x67f840 (la tabla de 02).
void WriteHeader(DumpWriter& writer, const CrashReport& report)
{
    writer.Bytes(Signature);
    writer.Byte(Version);
    writer.Dword(static_cast<std::uint32_t>(CrashHeaderSize));
    writer.Dword(report.time);
    writer.Text(report.system);
    writer.Text(report.processor);
    WriteMemoryStatus(writer, report.memory);
    writer.Dword(report.processId);
    writer.Text(report.executable);
    writer.Dword(report.executableCrc);
    writer.Dword(report.threadCount);
    writer.Byte(report.hasVersionInfo ? 1 : 0);
    writer.Dword(Capped(report.moduleCount, MaxCrashModules));
    WriteContext(writer, report.context);
    writer.Dword(report.exceptionCode);
    writer.Dword(report.exceptionAddress);
    writer.Text(report.exceptionText);
    writer.Dword(Capped(report.frameCount, MaxCrashFrames));
}

void WriteModule(DumpWriter& writer, const CrashModule& module)
{
    writer.Text(module.name);
    writer.Text(module.path);
    writer.Dword(module.base);
    writer.Dword(module.size);
}

void WriteFrame(DumpWriter& writer, const CrashFrame& frame)
{
    writer.Dword(frame.framePointer);
    writer.Dword(frame.returnAddress);
    for (const std::uint32_t value : frame.arguments)
    {
        writer.Dword(value);
    }
}
} // namespace

std::size_t CrashDumpSize(const CrashReport& report)
{
    const std::size_t version = report.hasVersionInfo ? VersionInfoSize : 0;
    return CrashHeaderSize + version + Capped(report.moduleCount, MaxCrashModules) * CrashModuleRecordSize +
           Capped(report.frameCount, MaxCrashFrames) * CrashFrameRecordSize;
}

std::size_t WriteCrashDump(const CrashReport& report, std::span<std::byte> output)
{
    if (output.size() < CrashDumpSize(report))
    {
        return 0;
    }
    DumpWriter writer(output);
    WriteHeader(writer, report);
    if (report.hasVersionInfo)
    {
        writer.Bytes(report.versionInfo);
    }
    const std::span<const CrashModule> modules(report.modules.data(), Capped(report.moduleCount, MaxCrashModules));
    for (const CrashModule& module : modules)
    {
        WriteModule(writer, module);
    }
    const std::span<const CrashFrame> frames(report.frames.data(), Capped(report.frameCount, MaxCrashFrames));
    for (const CrashFrame& frame : frames)
    {
        WriteFrame(writer, frame);
    }
    return writer.Position();
}
} // namespace Client::Core::Crash
