#include "Core/Log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Client::Core::Log y sus salidas: el prefijo de cada nivel, el mínimo, el TeeSink y el archivo.
namespace
{
namespace fs = std::filesystem;
using Client::Core::FileSink;
using Client::Core::ILogSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::TeeSink;

// Guarda cada línea que le llega, con su nivel. Se usa desde un solo hilo: no necesita mutex.
class RecordingSink final : public ILogSink
{
public:
    void Write(LogLevel level, std::string_view line) override
    {
        levels.push_back(level);
        lines.emplace_back(line);
    }

    std::vector<LogLevel> levels;
    std::vector<std::string> lines;
};

// Una carpeta temporal que se borra al salir del test. Con un sufijo al azar, dos corridas a la vez
// (Debug y Release, dos worktrees) no se pisan.
class TemporaryFolder
{
public:
    TemporaryFolder() : m_path(fs::temp_directory_path() / ("muret-log-" + std::to_string(std::random_device{}())))
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }

    ~TemporaryFolder()
    {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;

    [[nodiscard]] const fs::path& Path() const
    {
        return m_path;
    }

private:
    fs::path m_path;
};

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(LogTest, WritesTheLevelAndTheMessage)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Info("hola {}", 1);

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.lines[0], "[info] hola 1");
    EXPECT_EQ(sink.levels[0], LogLevel::Info);
}

TEST(LogTest, BelowTheMinimumIsNotWritten)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Warning);

    log.Debug("no");
    log.Info("tampoco");
    log.Warning("sí");
    log.Error("también");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[warning] sí", "[error] también"}));
}

TEST(LogTest, EachLevelHasItsPrefix)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Debug("d");
    log.Info("i");
    log.Warning("w");
    log.Error("e");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[debug] d", "[info] i", "[warning] w", "[error] e"}));
}

TEST(LogTest, TeeSinkWritesBoth)
{
    RecordingSink first;
    RecordingSink second;
    TeeSink tee(first, second);
    Log log(tee, LogLevel::Debug);

    log.Error("se cortó la conexión ({})", 10054);

    EXPECT_EQ(first.lines, (std::vector<std::string>{"[error] se cortó la conexión (10054)"}));
    EXPECT_EQ(second.lines, first.lines);
    EXPECT_EQ(first.levels, (std::vector<LogLevel>{LogLevel::Error}));
    EXPECT_EQ(second.levels, first.levels);
}

TEST(LogTest, FileSinkWritesOneLinePerMessage)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / "muret-client.log";
    std::ofstream(path) << "de una corrida anterior\n";

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    FileSink& file = *opened.Value();
    Log log(file, LogLevel::Info);

    log.Info("uno");
    log.Warning("dos {}", 2);

    // Se lee con el FileSink todavía abierto: cada línea ya está en el disco (el flush), y la
    // corrida anterior se borró al abrir.
    EXPECT_EQ(ReadText(path), "[info] uno\n[warning] dos 2\n");
}

TEST(LogTest, FileSinkNamesAPathThatDoesNotOpen)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / "no-existe" / "muret-client.log";

    const auto opened = FileSink::Open(path);

    ASSERT_FALSE(opened.HasValue());
    const std::u8string pathText = path.u8string();
    EXPECT_NE(opened.Error().find(std::string(pathText.begin(), pathText.end())), std::string::npos) << opened.Error();
}
} // namespace
