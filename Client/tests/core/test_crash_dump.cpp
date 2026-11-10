#include "Core/Crash/CrashDump.h"

#include "Core/Crash/CrashReport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Client::Core::Crash::WriteCrashDump: MuError.dmp byte a byte, contra un archivo armado a mano con
// los offsets que escribe 0x67f840 (02 § El manejador de caídas): cada campo en su lugar, sin
// relleno, en little-endian.
namespace
{
using Client::Core::Crash::CopyCrashText;
using Client::Core::Crash::CrashDumpSize;
using Client::Core::Crash::CrashFrameRecordSize;
using Client::Core::Crash::CrashHeaderSize;
using Client::Core::Crash::CrashModuleRecordSize;
using Client::Core::Crash::CrashReport;
using Client::Core::Crash::MaxCrashDumpSize;
using Client::Core::Crash::MaxCrashFrames;
using Client::Core::Crash::MaxCrashModules;
using Client::Core::Crash::VersionInfoSize;
using Client::Core::Crash::WriteCrashDump;
using Client::Core::Crash::X86ExtendedRegistersSize;

// Los offsets de la cabecera, como los ve el original sobre su buffer de 0x516 bytes.
constexpr std::size_t TimeOffset = 0x009;
constexpr std::size_t SystemOffset = 0x00d;
constexpr std::size_t ProcessorOffset = 0x08d;
constexpr std::size_t MemoryOffset = 0x10d;
constexpr std::size_t ProcessIdOffset = 0x12d;
constexpr std::size_t ExecutableOffset = 0x131;
constexpr std::size_t CrcOffset = 0x1b1;
constexpr std::size_t ThreadsOffset = 0x1b5;
constexpr std::size_t HasVersionOffset = 0x1b9;
constexpr std::size_t ModuleCountOffset = 0x1ba;
constexpr std::size_t ContextOffset = 0x1be;
constexpr std::size_t ExceptionCodeOffset = 0x48a;
constexpr std::size_t ExceptionAddressOffset = 0x48e;
constexpr std::size_t ExceptionTextOffset = 0x492;
constexpr std::size_t FrameCountOffset = 0x512;
// Dentro del CONTEXT de x86 (winnt.h).
constexpr std::size_t ContextDr7 = 0x18;
constexpr std::size_t ContextFloatSave = 0x1c;
constexpr std::size_t ContextSegGs = 0x8c;
constexpr std::size_t ContextEax = 0xb0;
constexpr std::size_t ContextEbp = 0xb4;
constexpr std::size_t ContextEip = 0xb8;
constexpr std::size_t ContextSegSs = 0xc8;
constexpr std::size_t ContextExtendedRegisters = 0xcc;
// Dentro de un módulo (0x67fe6a) y de un frame (0x67ffd1).
constexpr std::size_t ModulePathOffset = 0x80;
constexpr std::size_t ModuleBaseOffset = 0x180;
constexpr std::size_t ModuleSizeOffset = 0x184;
constexpr std::size_t FrameArgumentsOffset = 0x8;

// Un archivo esperado, armado campo por campo sobre bytes en 0.
class ExpectedFile
{
public:
    explicit ExpectedFile(std::size_t size) : m_bytes(size) {}

    void Byte(std::size_t offset, std::uint8_t value)
    {
        m_bytes.at(offset) = std::byte{value};
    }

    void Dword(std::size_t offset, std::uint32_t value)
    {
        constexpr unsigned BitsPerByte = 8;
        constexpr std::size_t DwordSize = 4;
        for (std::size_t i = 0; i < DwordSize; ++i)
        {
            Byte(offset + i, static_cast<std::uint8_t>(value >> (BitsPerByte * i)));
        }
    }

    void Text(std::size_t offset, std::string_view text)
    {
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            Byte(offset + i, static_cast<std::uint8_t>(text[i]));
        }
    }

    [[nodiscard]] const std::vector<std::byte>& Bytes() const
    {
        return m_bytes;
    }

private:
    std::vector<std::byte> m_bytes;
};

// Un reporte inventado: un sistema y un procesador, dos módulos y tres frames, con un acceso
// inválido al leer 0x00000010.
CrashReport SampleReport()
{
    CrashReport report;
    report.time = 0x4A3B2C1D;
    CopyCrashText(report.system, "Windows XP Build 2600(Service Pack 3)");
    CopyCrashText(report.processor, "GenuineIntel - Pentium 4 CPU 3.19GHz");
    report.memory = {.memoryLoad = 37,
                     .totalPhysical = 0x3FF00000,
                     .availablePhysical = 0x1F000000,
                     .totalPageFile = 0x7FF00000,
                     .availablePageFile = 0x60000000,
                     .totalVirtual = 0x7FFE0000,
                     .availableVirtual = 0x7A000000};
    report.processId = 0x0F2C;
    CopyCrashText(report.executable, "main.exe");
    report.executableCrc = 0xC32BBD5F;
    report.threadCount = 9;
    report.moduleCount = 2;
    CopyCrashText(report.modules[0].name, "main.exe");
    CopyCrashText(report.modules[0].path, "C:\\MU\\main.exe");
    report.modules[0].base = 0x00400000;
    report.modules[0].size = 0x0047D000;
    CopyCrashText(report.modules[1].name, "ntdll.dll");
    CopyCrashText(report.modules[1].path, "C:\\WINDOWS\\system32\\ntdll.dll");
    report.modules[1].base = 0x7C900000;
    report.modules[1].size = 0x000B2000;
    report.context.contextFlags = 0x0001003F;
    report.context.dr7 = 0x00000155;
    report.context.floatSave[0] = std::byte{0x7F};
    report.context.segGs = 0x0000;
    report.context.eax = 0x00000010;
    report.context.ebp = 0x0012FF30;
    report.context.eip = 0x004B9C12;
    report.context.segSs = 0x0023;
    report.context.extendedRegisters[0] = std::byte{0x7F};
    report.context.extendedRegisters[X86ExtendedRegistersSize - 1] = std::byte{0xEE};
    report.exceptionCode = 0xC0000005;
    report.exceptionAddress = 0x004B9C12;
    CopyCrashText(report.exceptionText, "Access violation : reading inaccessible 0x00000010");
    report.frameCount = 3;
    for (std::uint32_t i = 0; i < report.frameCount; ++i)
    {
        report.frames[i].framePointer = 0x0012FF30 + i * 0x20;
        report.frames[i].returnAddress = 0x004B9000 + i;
        report.frames[i].arguments[0] = 0xA0 + i;
        report.frames[i].arguments[15] = 0xF0 + i;
    }
    return report;
}
} // namespace

// Lo que mide cada parte: la cabecera es la del campo 0x005, y los registros, los de los fwrite.
TEST(CrashDumpTest, RecordSizesAreTheOriginals)
{
    EXPECT_EQ(CrashHeaderSize, 0x516u);
    EXPECT_EQ(VersionInfoSize, 0x34u);
    EXPECT_EQ(CrashModuleRecordSize, 0x188u);
    EXPECT_EQ(CrashFrameRecordSize, 0x48u);
    EXPECT_EQ(MaxCrashDumpSize, 0x516u + 0x34u + 48u * 0x188u + 64u * 0x48u);
}

TEST(CrashDumpTest, WritesTheHeaderModulesAndFramesByteForByte)
{
    const CrashReport report = SampleReport();
    const std::size_t size = CrashHeaderSize + 2 * CrashModuleRecordSize + 3 * CrashFrameRecordSize;
    ExpectedFile expected(size);
    expected.Text(0, "WDMP");
    expected.Byte(4, 2);
    expected.Dword(5, 0x516);
    expected.Dword(TimeOffset, 0x4A3B2C1D);
    expected.Text(SystemOffset, "Windows XP Build 2600(Service Pack 3)");
    expected.Text(ProcessorOffset, "GenuineIntel - Pentium 4 CPU 3.19GHz");
    const std::array<std::uint32_t, 8> memory{0x20,       37,         0x3FF00000, 0x1F000000,
                                              0x7FF00000, 0x60000000, 0x7FFE0000, 0x7A000000};
    for (std::size_t i = 0; i < memory.size(); ++i)
    {
        expected.Dword(MemoryOffset + 4 * i, memory[i]);
    }
    expected.Dword(ProcessIdOffset, 0x0F2C);
    expected.Text(ExecutableOffset, "main.exe");
    expected.Dword(CrcOffset, 0xC32BBD5F);
    expected.Dword(ThreadsOffset, 9);
    expected.Byte(HasVersionOffset, 0);
    expected.Dword(ModuleCountOffset, 2);
    expected.Dword(ContextOffset, 0x0001003F);
    expected.Dword(ContextOffset + ContextDr7, 0x155);
    expected.Byte(ContextOffset + ContextFloatSave, 0x7F);
    expected.Dword(ContextOffset + ContextSegGs, 0);
    expected.Dword(ContextOffset + ContextEax, 0x10);
    expected.Dword(ContextOffset + ContextEbp, 0x0012FF30);
    expected.Dword(ContextOffset + ContextEip, 0x004B9C12);
    expected.Dword(ContextOffset + ContextSegSs, 0x23);
    expected.Byte(ContextOffset + ContextExtendedRegisters, 0x7F);
    expected.Byte(ContextOffset + ContextExtendedRegisters + X86ExtendedRegistersSize - 1, 0xEE);
    expected.Dword(ExceptionCodeOffset, 0xC0000005);
    expected.Dword(ExceptionAddressOffset, 0x004B9C12);
    expected.Text(ExceptionTextOffset, "Access violation : reading inaccessible 0x00000010");
    expected.Dword(FrameCountOffset, 3);

    const std::size_t modules = CrashHeaderSize;
    expected.Text(modules, "main.exe");
    expected.Text(modules + ModulePathOffset, "C:\\MU\\main.exe");
    expected.Dword(modules + ModuleBaseOffset, 0x00400000);
    expected.Dword(modules + ModuleSizeOffset, 0x0047D000);
    expected.Text(modules + CrashModuleRecordSize, "ntdll.dll");
    expected.Text(modules + CrashModuleRecordSize + ModulePathOffset, "C:\\WINDOWS\\system32\\ntdll.dll");
    expected.Dword(modules + CrashModuleRecordSize + ModuleBaseOffset, 0x7C900000);
    expected.Dword(modules + CrashModuleRecordSize + ModuleSizeOffset, 0x000B2000);

    const std::size_t frames = modules + 2 * CrashModuleRecordSize;
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        const std::size_t frame = frames + i * CrashFrameRecordSize;
        expected.Dword(frame, 0x0012FF30 + i * 0x20);
        expected.Dword(frame + 4, 0x004B9000 + i);
        expected.Dword(frame + FrameArgumentsOffset, 0xA0 + i);
        expected.Dword(frame + FrameArgumentsOffset + 15 * 4, 0xF0 + i);
    }

    std::vector<std::byte> output(MaxCrashDumpSize);
    ASSERT_EQ(CrashDumpSize(report), size);
    ASSERT_EQ(WriteCrashDump(report, output), size);
    output.resize(size);
    EXPECT_EQ(output, expected.Bytes());
}

// Con la versión, sus 0x34 bytes van entre la cabecera y los módulos (0x67ff7a), y el byte 0x1b9
// es 1.
TEST(CrashDumpTest, PutsTheVersionInfoBeforeTheModules)
{
    CrashReport report;
    report.hasVersionInfo = true;
    report.versionInfo[0] = std::byte{0xBD};
    report.versionInfo[VersionInfoSize - 1] = std::byte{0x04};
    report.moduleCount = 1;
    CopyCrashText(report.modules[0].name, "main.exe");

    std::vector<std::byte> output(MaxCrashDumpSize);
    const std::size_t size = WriteCrashDump(report, output);

    ASSERT_EQ(size, CrashHeaderSize + VersionInfoSize + CrashModuleRecordSize);
    EXPECT_EQ(output[HasVersionOffset], std::byte{1});
    EXPECT_EQ(output[CrashHeaderSize], std::byte{0xBD});
    EXPECT_EQ(output[CrashHeaderSize + VersionInfoSize - 1], std::byte{0x04});
    EXPECT_EQ(output[CrashHeaderSize + VersionInfoSize], std::byte{'m'});
}

// Sin módulos ni frames, el archivo es solo la cabecera, con las dos cuentas en 0.
TEST(CrashDumpTest, AnEmptyReportIsJustTheHeader)
{
    std::vector<std::byte> output(MaxCrashDumpSize, std::byte{0xCC});

    ASSERT_EQ(WriteCrashDump(CrashReport{}, output), CrashHeaderSize);
    EXPECT_EQ(output[0], std::byte{'W'});
    EXPECT_EQ(output[MemoryOffset], std::byte{0x20});
    EXPECT_EQ(output[ModuleCountOffset], std::byte{0});
    EXPECT_EQ(output[FrameCountOffset], std::byte{0});
    // Los textos vacíos van enteros en 0.
    EXPECT_TRUE(std::all_of(output.begin() + SystemOffset, output.begin() + ProcessorOffset,
                            [](std::byte value) { return value == std::byte{0}; }));
    // Lo que sigue a la cabecera no se toca.
    EXPECT_EQ(output[CrashHeaderSize], std::byte{0xCC});
}

// Una cuenta de más se toma como el tope: el original nunca junta más de 48 y 64.
TEST(CrashDumpTest, CapsTheCountsAtTheOriginalLimits)
{
    CrashReport report;
    report.moduleCount = 1000;
    report.frameCount = 1000;
    std::vector<std::byte> output(MaxCrashDumpSize);

    ASSERT_EQ(WriteCrashDump(report, output),
              CrashHeaderSize + MaxCrashModules * CrashModuleRecordSize + MaxCrashFrames * CrashFrameRecordSize);
    EXPECT_EQ(output[ModuleCountOffset], std::byte{48});
    EXPECT_EQ(output[FrameCountOffset], std::byte{64});
}

// Un buffer que no alcanza: no escribe nada y devuelve 0.
TEST(CrashDumpTest, ReturnsZeroWhenTheBufferIsTooSmall)
{
    std::vector<std::byte> output(CrashHeaderSize - 1, std::byte{0xCC});

    EXPECT_EQ(WriteCrashDump(CrashReport{}, output), 0u);
    EXPECT_EQ(output[0], std::byte{0xCC});
}
