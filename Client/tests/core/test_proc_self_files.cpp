#include "Diagnostics/ProcSelfFiles.h"

#include "Diagnostics/CrashReport.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace
{
using Client::Diagnostics::CrashModule;
using Client::Diagnostics::CrashModulePathSize;
using Client::Diagnostics::MaxCrashModules;
using Client::Diagnostics::ModulesFromProcMaps;
using Client::Diagnostics::ThreadCountFromProcStat;

std::string_view Text(const auto& field)
{
    const std::string_view all(field.data(), field.size());
    return all.substr(0, all.find('\0'));
}

constexpr std::string_view Maps =
    "5581a3a00000-5581a3a42000 r--p 00000000 08:02 1311          /opt/muret/Main\n"
    "5581a3a42000-5581a3c00000 r-xp 00042000 08:02 1311          /opt/muret/Main\n"
    "5581a3c00000-5581a3c10000 rw-p 001c0000 08:02 1311          /opt/muret/Main\n"
    "5581a3c10000-5581a3c20000 rw-p 00000000 00:00 0 \n"
    "5581a4000000-5581a4100000 rw-p 00000000 00:00 0             [heap]\n"
    "7f2c10000000-7f2c10028000 r--p 00000000 08:02 2000          /usr/lib/x86_64-linux-gnu/libc.so.6\n"
    "7f2c10028000-7f2c101bd000 r-xp 00028000 08:02 2000          /usr/lib/x86_64-linux-gnu/libc.so.6\n"
    "7ffc5a100000-7ffc5a121000 rw-p 00000000 00:00 0             [stack]\n"
    "7ffc5a1f0000-7ffc5a1f2000 r-xp 00000000 00:00 0             [vdso]\n";
} // namespace

// Anonymous mappings and [heap], [stack] and [vdso] are not modules.
TEST(ProcSelfFilesTest, ListsEachMappedFileOnce)
{
    std::array<CrashModule, MaxCrashModules> modules{};

    ASSERT_EQ(ModulesFromProcMaps(Maps, modules), 2u);
    EXPECT_EQ(Text(modules[0].name), "Main");
    EXPECT_EQ(Text(modules[0].path), "/opt/muret/Main");
    // The low 32 bits of 0x5581a3a00000, and from there up to 0x5581a3c10000.
    EXPECT_EQ(modules[0].base, 0xa3a00000u);
    EXPECT_EQ(modules[0].size, 0x210000u);
    EXPECT_EQ(Text(modules[1].name), "libc.so.6");
    EXPECT_EQ(Text(modules[1].path), "/usr/lib/x86_64-linux-gnu/libc.so.6");
    EXPECT_EQ(modules[1].base, 0x10000000u);
    EXPECT_EQ(modules[1].size, 0x1bd000u);
}

// Whole, because it is the last field of the line.
TEST(ProcSelfFilesTest, KeepsSpacesInAPath)
{
    std::array<CrashModule, MaxCrashModules> modules{};

    ASSERT_EQ(ModulesFromProcMaps("1000-2000 r--p 00000000 08:02 7    /home/user/Mu Ret/Main", modules), 1u);
    EXPECT_EQ(Text(modules[0].path), "/home/user/Mu Ret/Main");
    EXPECT_EQ(Text(modules[0].name), "Main");
}

// Up to 48, like the original; the rest are lost.
TEST(ProcSelfFilesTest, StopsAt48Modules)
{
    std::string maps;
    for (int i = 0; i < 60; ++i)
    {
        maps += std::to_string(i + 1) + "000-" + std::to_string(i + 1) + "800 r--p 0 0:0 1 /lib/module" +
                std::to_string(i) + ".so\n";
    }
    std::array<CrashModule, MaxCrashModules> modules{};

    ASSERT_EQ(ModulesFromProcMaps(maps, modules), MaxCrashModules);
    EXPECT_EQ(Text(modules[47].name), "module47.so");
}

TEST(ProcSelfFilesTest, CutsALongPathAndStillJoinsItsMappings)
{
    const std::string path = "/" + std::string(300, 'p');
    const std::string maps = "1000-2000 r--p 0 0:0 1 " + path + "\n3000-5000 r-xp 0 0:0 1 " + path + "\n";
    std::array<CrashModule, MaxCrashModules> modules{};

    ASSERT_EQ(ModulesFromProcMaps(maps, modules), 1u);
    EXPECT_EQ(Text(modules[0].path).size(), CrashModulePathSize - 1);
    EXPECT_EQ(modules[0].size, 0x4000u);
}

TEST(ProcSelfFilesTest, SkipsMalformedLines)
{
    std::array<CrashModule, MaxCrashModules> modules{};

    EXPECT_EQ(ModulesFromProcMaps("garbage\nzz-10 r--p 0 0:0 1 /x\n2000-1000 r--p 0 0:0 1 /y\n\n", modules), 0u);
}

// Counted from the last ')': the name can hold spaces and parentheses.
TEST(ProcSelfFilesTest, ReadsTheThreadCountFromStat)
{
    const std::string stat = "4242 (Main (x) y) S 1 4242 4242 0 -1 4194560 2181 0 0 0 12 3 0 0 20 0 7 0 "
                             "1903311 1234567 890 18446744073709551615";

    EXPECT_EQ(ThreadCountFromProcStat(stat), std::optional<std::uint32_t>(7));
}

TEST(ProcSelfFilesTest, GivesNoThreadCountForATruncatedStat)
{
    EXPECT_EQ(ThreadCountFromProcStat(""), std::nullopt);
    EXPECT_EQ(ThreadCountFromProcStat("4242 Main S 1"), std::nullopt);
    EXPECT_EQ(ThreadCountFromProcStat("4242 (Main) S 1 4242"), std::nullopt);
}
