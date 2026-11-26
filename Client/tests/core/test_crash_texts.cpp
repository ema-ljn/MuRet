#include "Diagnostics/CrashTexts.h"

#include "Diagnostics/CrashReport.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

// The system of 0x683ed0, the processor of 0x684730, the exception of 0x680260 and the log line of 0x4b9b34.
namespace
{
using Client::Diagnostics::CpuId;
using Client::Diagnostics::CrashLogLine;
using Client::Diagnostics::CrashText;
using Client::Diagnostics::CrashTextSize;
using Client::Diagnostics::ExceptionText;
using Client::Diagnostics::MegahertzFromSample;
using Client::Diagnostics::OsVersion;
using Client::Diagnostics::PosixSystemText;
using Client::Diagnostics::ProcessorText;
using Client::Diagnostics::SystemText;

std::string_view Text(const CrashText& text)
{
    const std::string_view all(text.data(), text.size());
    return all.substr(0, all.find('\0'));
}

constexpr std::uint32_t Nt = 2;
constexpr std::uint32_t Windows9x = 1;

// The cpuid vendor, in EBX, EDX and ECX.
constexpr CpuId Intel{.vendorEbx = 0x756e6547, .vendorEdx = 0x49656e69, .vendorEcx = 0x6c65746e, .signature = 0};
constexpr CpuId Amd{.vendorEbx = 0x68747541, .vendorEdx = 0x69746e65, .vendorEcx = 0x444d4163, .signature = 0};

CpuId WithSignature(CpuId cpu, std::uint32_t family, std::uint32_t model)
{
    cpu.signature = (family << 8) | (model << 4);
    return cpu;
}
} // namespace

TEST(CrashTextsTest, NamesWindowsXpWithItsBuildAndServicePack)
{
    const OsVersion xp{.major = 5, .minor = 1, .build = 2600, .platformId = Nt, .servicePack = "Service Pack 3"};

    EXPECT_EQ(Text(SystemText(xp)), "Windows XP Build 2600(Service Pack 3)");
}

TEST(CrashTextsTest, NamesTheNtFamily)
{
    EXPECT_EQ(Text(SystemText({.major = 3, .minor = 51, .build = 1057, .platformId = Nt})),
              "Windows NT 3.51 Build 1057");
    EXPECT_EQ(
        Text(SystemText({.major = 4, .minor = 0, .build = 1381, .platformId = Nt, .servicePack = "Service Pack 6"})),
        "Windows NT 4.0 Build 1381(Service Pack 6)");
    EXPECT_EQ(Text(SystemText({.major = 5, .minor = 2, .build = 3790, .platformId = Nt})),
              "Windows 2003 family Build 3790");
}

// Case-insensitive, as _stricmp.
TEST(CrashTextsTest, AddsTheWindows2000ProductType)
{
    const auto windows2000 = [](std::string_view productType)
    {
        return std::string(
            Text(SystemText({.major = 5, .minor = 0, .build = 2195, .platformId = Nt, .productType = productType})));
    };

    EXPECT_EQ(windows2000("WinNT"), "Windows 2000 Professional Build 2195");
    EXPECT_EQ(windows2000("LANMANNT"), "Windows 2000 Server Build 2195");
    EXPECT_EQ(windows2000("ServerNT"), "Windows 2000 Advanced Server Build 2195");
    EXPECT_EQ(windows2000(""), "Windows 2000 Build 2195");
}

// On 95, 98 and Me the build is split, with a trailing space, and the service pack letter names the edition.
TEST(CrashTextsTest, SplitsTheBuildOfWindows9x)
{
    EXPECT_EQ(
        Text(SystemText({.major = 4, .minor = 0, .build = 0x04000457, .platformId = Windows9x, .servicePack = " B"})),
        "Windows 95 OSR2 Build 4.0.1111 ( B)");
    EXPECT_EQ(
        Text(SystemText({.major = 4, .minor = 10, .build = 0x040A08AE, .platformId = Windows9x, .servicePack = " A "})),
        "Windows 98 SE Build 4.10.2222 ( A )");
    EXPECT_EQ(Text(SystemText({.major = 4, .minor = 90, .build = 0x045A0BB8, .platformId = Windows9x})),
              "Windows Me Build 4.90.3000 ");
}

// What it does not know carries the version: that is how any Windows after XP comes out, since GetVersionExA tells it
// 6.2.
TEST(CrashTextsTest, CallsANewerSystemUnknown)
{
    EXPECT_EQ(Text(SystemText({.major = 6, .minor = 2, .build = 9200, .platformId = Nt})), "Unknown 6.2 Build 9200");
    EXPECT_EQ(Text(SystemText({.major = 4, .minor = 3, .build = 7, .platformId = Nt})), "Unknown 4.3 Build 7");
}

TEST(CrashTextsTest, NamesALinuxSystemWithUname)
{
    EXPECT_EQ(Text(PosixSystemText("Linux", "6.8.0-45-generic")), "Linux 6.8.0-45-generic");
}

// Like the lstrcpynA that copies it: 127 bytes and the 0.
TEST(CrashTextsTest, CutsATextAt127Bytes)
{
    const std::string release(200, 'r');

    const CrashText text = PosixSystemText("Linux", release);

    EXPECT_EQ(Text(text).size(), CrashTextSize - 1);
    EXPECT_EQ(text.back(), '\0');
}

TEST(CrashTextsTest, NamesIntelProcessors)
{
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 15, 2), 3192)), "GenuineIntel - Pentium 4 CPU 3.19GHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 6, 8), 450)), "GenuineIntel - Pentium 3 CPU 450MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 6, 6), 400)), "GenuineIntel - Pentium Celeron CPU 400MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 6, 3), 300)), "GenuineIntel - Pentium 2 CPU 300MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 6, 1), 200)), "GenuineIntel - Pentium Pro CPU 200MHz");
    // From family 3 to 5 the table says nothing: the two spaces remain.
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 5, 2), 133)), "GenuineIntel -  CPU 133MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Intel, 7, 0), 900)), "GenuineIntel - Unknown CPU 900MHz");
}

TEST(CrashTextsTest, NamesAmdProcessors)
{
    EXPECT_EQ(Text(ProcessorText(WithSignature(Amd, 6, 4), 1400)), "AuthenticAMD - AMD K-7 Athlon CPU 1.40GHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Amd, 6, 7), 1200)), "AuthenticAMD - AMD K-7 Duron CPU 1.20GHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Amd, 5, 8), 500)), "AuthenticAMD - AMD K6-2 CPU 500MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Amd, 4, 14), 133)), "AuthenticAMD - AMD 586 CPU 133MHz");
    EXPECT_EQ(Text(ProcessorText(WithSignature(Amd, 15, 0), 2000)), "AuthenticAMD - AMD Unknown CPU 2.00GHz");
}

// Exactly 1000 MHz still goes in MHz.
TEST(CrashTextsTest, CallsOtherVendorsUnknown)
{
    const CpuId other{.vendorEbx = 0x746e6543, .vendorEdx = 0x48727561, .vendorEcx = 0x736c7561, .signature = 0x6F0};

    EXPECT_EQ(Text(ProcessorText(other, 1000)), "Unknown CPU 1000MHz");
    EXPECT_EQ(Text(ProcessorText(CpuId{}, 0)), "Unknown CPU 0MHz");
    EXPECT_EQ(Text(ProcessorText(other, 1001)), "Unknown CPU 1.00GHz");
}

TEST(CrashTextsTest, MeasuresMegahertzFromASample)
{
    // 159,600,000 cycles in 50 ms: 3,192,000,000 Hz.
    EXPECT_EQ(MegahertzFromSample(159600000, 50000000, 1000000000), 3192);
    // With an XP's QueryPerformanceCounter frequency (3,579,545 Hz).
    EXPECT_EQ(MegahertzFromSample(22500000, 178977, 3579545), 450);
    EXPECT_EQ(MegahertzFromSample(1000, 0, 1000000000), 0);
    EXPECT_EQ(MegahertzFromSample(1000, -5, 1000000000), 0);
    EXPECT_EQ(MegahertzFromSample(1000, 5, 0), 0);
}

TEST(CrashTextsTest, DescribesAnAccessViolationWithItsAddress)
{
    EXPECT_EQ(Text(ExceptionText(0xC0000005, 0, 0x10)), "Access violation : reading inaccessible 0x00000010");
    EXPECT_EQ(Text(ExceptionText(0xC0000005, 1, 0xDEADBEEF)), "Access violation : writing inaccessible 0xDEADBEEF");
    // Any value other than 0 is a write, DEP's 8 too.
    EXPECT_EQ(Text(ExceptionText(0xC0000005, 8, 0x401000)), "Access violation : writing inaccessible 0x00401000");
    // MuRet is 64-bit: the whole address goes in.
    EXPECT_EQ(Text(ExceptionText(0xC0000005, 0, 0x7FFE12345678)),
              "Access violation : reading inaccessible 0x7FFE12345678");
}

TEST(CrashTextsTest, NamesTheOtherExceptions)
{
    EXPECT_EQ(Text(ExceptionText(0xC0000094, 0, 0x10)), "Int divide by zero");
    EXPECT_EQ(Text(ExceptionText(0xC00000FD, 0, 0)), "Stack overflow");
    EXPECT_EQ(Text(ExceptionText(0xC000001D, 0, 0)), "Illegal instruction");
    EXPECT_EQ(Text(ExceptionText(0x80000003, 0, 0)), "Break point");
    EXPECT_EQ(Text(ExceptionText(0xC0000093, 0, 0)), "Float underflow");
    EXPECT_EQ(Text(ExceptionText(0xE06D7363, 0, 0)), "Unknown exception");
}

// The 0x6c80b4 message without the "\r\n", with Log::Error's prefix.
TEST(CrashTextsTest, WritesTheLogLineLikeLogError)
{
    EXPECT_EQ(CrashLogLine(), "[error] Save DumpFile complete - MuError.dmp");
}
