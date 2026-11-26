#include "Data/DataPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
namespace fs = std::filesystem;

class DataPathTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // A random suffix keeps two simultaneous runs (Debug and Release, two worktrees) from deleting each other's
        // folder.
        const std::string testName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        m_root =
            fs::temp_directory_path() / ("muret-datapath-" + testName + "-" + std::to_string(std::random_device{}()));
        fs::remove_all(m_root);
        fs::create_directories(m_root);
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(m_root, error);
    }

    // Not CreateFile: on Windows <windows.h> defines it as a macro (CreateFileW).
    void WriteDataFile(const fs::path& relative) const
    {
        fs::create_directories((m_root / relative).parent_path());
        std::ofstream(m_root / relative) << "x";
    }

    // An I/O error here is a test failure.
    std::optional<fs::path> Resolve(std::string_view originalPath) const
    {
        std::error_code error;
        std::optional<fs::path> path = Client::Data::ResolveDataPath(m_root, originalPath, error);
        EXPECT_FALSE(error) << originalPath << ": " << error.message();
        return path;
    }

    // ext4 does not; NTFS and APFS do by default.
    bool FileSystemIgnoresCase() const
    {
        WriteDataFile("CaseProbe");
        std::error_code error;
        const bool ignoresCase = fs::exists(m_root / "caseprobe", error);
        fs::remove(m_root / "CaseProbe", error);
        return ignoresCase;
    }

    fs::path m_root;
};

TEST_F(DataPathTest, FindsAnExactPath)
{
    WriteDataFile("Player/Player.bmd");

    const auto path = Resolve("Player/Player.bmd");

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, m_root / "Player" / "Player.bmd");
}

TEST_F(DataPathTest, AcceptsBackslashesAndIgnoresCaseLikeWindows)
{
    // As in the lab's Data\: the folder lowercase and the extension uppercase.
    WriteDataFile("player/Player.BMD");

    const auto path = Resolve("Player\\player.bmd");

    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(fs::is_regular_file(*path));
    // The returned name is not compared: on Linux it is the disk's ("Player.BMD"), but on Windows the first attempt
    // already finds it with the requested case.
    EXPECT_TRUE(fs::equivalent(*path, m_root / "player" / "Player.BMD"));
}

TEST_F(DataPathTest, SkipsDotsAndEmptyParts)
{
    WriteDataFile("Player/Player.bmd");

    const auto path = Resolve(".\\Player\\\\.\\Player.bmd");

    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(fs::equivalent(*path, m_root / "Player" / "Player.bmd"));
}

TEST_F(DataPathTest, StopsAtTheFirstNulLikeACString)
{
    WriteDataFile("Player/Player.bmd");
    // A .bmd fixed-length name, zero-padded.
    constexpr std::string_view FixedSizeName("Player\\Player.bmd\0\0\0", 20);

    const auto path = Resolve(FixedSizeName);

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, m_root / "Player" / "Player.bmd");
}

TEST_F(DataPathTest, ReturnsNulloptWhenTheFileIsMissing)
{
    WriteDataFile("Player/Player.bmd");

    EXPECT_FALSE(Resolve("Player\\Player02.bmd").has_value());
    EXPECT_FALSE(Resolve("Monster\\Monster50.bmd").has_value());
}

TEST_F(DataPathTest, RejectsPathsThatAreNotDataFiles)
{
    WriteDataFile("Enc1.dat");
    // Player\ exists, so "Player\..\.." fails for the "..", not for a missing folder.
    WriteDataFile("Player/Player.bmd");

    EXPECT_FALSE(Resolve("").has_value());
    EXPECT_FALSE(Resolve("..\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("Player\\..\\..\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("/Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("C:\\Enc1.dat").has_value());
    // Windows does not open "file\" either (ERROR_INVALID_NAME).
    EXPECT_FALSE(Resolve("Enc1.dat\\").has_value());
    // Only dots: Data\ itself, not something inside it.
    EXPECT_FALSE(Resolve(".").has_value());
    EXPECT_FALSE(Resolve(".\\.").has_value());
}

TEST_F(DataPathTest, IgnoresBrokenSymlinks)
{
    std::error_code error;
    fs::create_symlink(m_root / "NoExiste.bmd", m_root / "Roto.bmd", error);
    if (error)
    {
        GTEST_SKIP() << "no se pudo crear el symlink (en Windows hace falta permiso): " << error.message();
    }

    // The symlink shows up when walking the folder but does not exist.
    EXPECT_FALSE(Resolve("Roto.bmd").has_value());
    EXPECT_FALSE(Resolve("ROTO.BMD").has_value());
}

TEST_F(DataPathTest, PrefersTheExactNameWhenBothCasesExist)
{
    if (FileSystemIgnoresCase())
    {
        GTEST_SKIP() << "este sistema de archivos no distingue mayúsculas";
    }
    WriteDataFile("Item/a.bmd");
    WriteDataFile("item/b.bmd");

    EXPECT_EQ(Resolve("Item\\a.bmd"), m_root / "Item" / "a.bmd");
    EXPECT_EQ(Resolve("item\\b.bmd"), m_root / "item" / "b.bmd");
    // Without an exact match, the first in byte order: "Item" < "item".
    EXPECT_EQ(Resolve("ITEM\\a.bmd"), m_root / "Item" / "a.bmd");
}

TEST_F(DataPathTest, FoldsOnlyAsciiLetters)
{
    if (FileSystemIgnoresCase())
    {
        GTEST_SKIP() << "este sistema de archivos ya compara sin mayúsculas, también fuera de ASCII";
    }
    WriteDataFile(fs::path(u8"\u00C4.bmd")); // "Ä.bmd"

    // "Ä" is C3 84 and "ä" is C3 A4 in UTF-8: not ASCII letters, so they do not match.
    EXPECT_FALSE(Resolve("\xC3\xA4.bmd").has_value());
    EXPECT_TRUE(Resolve("\xC3\x84.BMD").has_value());
}

TEST_F(DataPathTest, ReportsFoldersThatCannotBeRead)
{
    WriteDataFile("Enc1.dat");
    std::error_code error;

    // The Data\ "folder" is a file: it cannot be walked.
    EXPECT_FALSE(Client::Data::ResolveDataPath(m_root / "Enc1.dat", "Player.bmd", error).has_value());
    EXPECT_TRUE(error);

    EXPECT_FALSE(Client::Data::ResolveDataPath(m_root / "NoData", "Player.bmd", error).has_value());
    EXPECT_TRUE(error);

    // A caller error, not "missing".
    EXPECT_FALSE(Client::Data::ResolveDataPath(fs::path(), "Player.bmd", error).has_value());
    EXPECT_EQ(error, std::make_error_code(std::errc::invalid_argument));
}
} // namespace
