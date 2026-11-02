#include "Data/DataPath.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

// Client::Data::ResolveDataPath contra un Data\ armado en una carpeta temporal.
namespace
{
namespace fs = std::filesystem;

class DataPathTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Con un sufijo al azar, dos corridas a la vez (Debug y Release, dos worktrees) no se
        // borran la carpeta una a la otra.
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

    // No se llama CreateFile: en Windows <windows.h> la define como macro (CreateFileW).
    void WriteDataFile(const fs::path& relative) const
    {
        fs::create_directories((m_root / relative).parent_path());
        std::ofstream(m_root / relative) << "x";
    }

    // ResolveDataPath sin errores de E/S: un error acá es una falla del test.
    std::optional<fs::path> Resolve(std::string_view originalPath) const
    {
        std::error_code error;
        std::optional<fs::path> path = Client::Data::ResolveDataPath(m_root, originalPath, error);
        EXPECT_FALSE(error) << originalPath << ": " << error.message();
        return path;
    }

    // ¿Este sistema de archivos distingue mayúsculas? (ext4 sí; NTFS y APFS, por defecto, no.)
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
    // Como en el Data\ del laboratorio: la carpeta en minúscula y la extensión en mayúscula.
    WriteDataFile("player/Player.BMD");

    const auto path = Resolve("Player\\player.bmd");

    ASSERT_TRUE(path.has_value());
    ASSERT_TRUE(fs::is_regular_file(*path));
    // Es el mismo archivo. El nombre devuelto no se compara: en Linux es el del disco
    // ("Player.BMD"), pero en Windows el primer intento ya lo encuentra con la grafía pedida.
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
    // Un nombre de largo fijo de un .bmd, completado con ceros.
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
    // Player\ existe, así que "Player\..\.." falla por el "..", no porque falte la carpeta.
    WriteDataFile("Player/Player.bmd");

    EXPECT_FALSE(Resolve("").has_value());
    EXPECT_FALSE(Resolve("..\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("Player\\..\\..\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("\\Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("/Enc1.dat").has_value());
    EXPECT_FALSE(Resolve("C:\\Enc1.dat").has_value());
    // Windows tampoco abre "archivo\" (ERROR_INVALID_NAME).
    EXPECT_FALSE(Resolve("Enc1.dat\\").has_value());
    // Solo puntos: es Data\ misma, no algo adentro.
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

    // Tal cual y sin mayúsculas: el symlink aparece al recorrer la carpeta, pero no existe.
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
    // Sin coincidencia exacta, la primera en orden de bytes: "Item" < "item".
    EXPECT_EQ(Resolve("ITEM\\a.bmd"), m_root / "Item" / "a.bmd");
}

TEST_F(DataPathTest, FoldsOnlyAsciiLetters)
{
    if (FileSystemIgnoresCase())
    {
        GTEST_SKIP() << "este sistema de archivos ya compara sin mayúsculas, también fuera de ASCII";
    }
    WriteDataFile(fs::path(u8"\u00C4.bmd")); // "Ä.bmd"

    // "Ä" es C3 84 y "ä" es C3 A4 en UTF-8: no son letras ASCII, así que no se igualan.
    EXPECT_FALSE(Resolve("\xC3\xA4.bmd").has_value());
    EXPECT_TRUE(Resolve("\xC3\x84.BMD").has_value());
}

TEST_F(DataPathTest, ReportsFoldersThatCannotBeRead)
{
    WriteDataFile("Enc1.dat");
    std::error_code error;

    // La "carpeta" Data\ es un archivo: no se puede recorrer.
    EXPECT_FALSE(Client::Data::ResolveDataPath(m_root / "Enc1.dat", "Player.bmd", error).has_value());
    EXPECT_TRUE(error);

    // No existe la carpeta Data\.
    EXPECT_FALSE(Client::Data::ResolveDataPath(m_root / "NoData", "Player.bmd", error).has_value());
    EXPECT_TRUE(error);

    // Sin carpeta: un error de quien llama, no "no está".
    EXPECT_FALSE(Client::Data::ResolveDataPath(fs::path(), "Player.bmd", error).has_value());
    EXPECT_EQ(error, std::make_error_code(std::errc::invalid_argument));
}
} // namespace
