#include "Core/FileSink.h"
#include "Core/Log.h"
#include "Core/PathText.h"
#include "Core/StderrSink.h"
#include "Core/TeeSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// Client::Core::Log y sus salidas: el prefijo de cada nivel, el mínimo, los formatos rotos, el
// TeeSink, stderr y el archivo.
namespace
{
namespace fs = std::filesystem;
using Client::Core::FileSink;
using Client::Core::ILogSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::MaxLogFileBytes;
using Client::Core::PathText;
using Client::Core::StderrSink;
using Client::Core::TeeSink;

// "ñ" en UTF-8, para las rutas que no son ASCII (PathText y, en Windows, la ruta ancha de abrir).
constexpr std::string_view EnyeUtf8 = "\xC3\xB1";

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

std::vector<std::string> ReadLines(const fs::path& path)
{
    std::ifstream file(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);)
    {
        lines.push_back(line);
    }
    return lines;
}

struct FileCloser
{
    void operator()(std::FILE* file) const
    {
        std::fclose(file);
    }
};

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

TEST(LogTest, WriteKeepsTheBraces)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Write(LogLevel::Info, "{} {:x} }{");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[info] {} {:x} }{"}));
}

TEST(LogTest, WriteBelowTheMinimumIsNotWritten)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Warning);

    log.Write(LogLevel::Info, "no");

    EXPECT_TRUE(sink.lines.empty());
}

TEST(LogTest, ABadFormatIsLoggedAsAnError)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    // {:c} pasa al compilar, pero 300000 no es una letra: std::vformat tira std::format_error.
    EXPECT_NO_THROW(log.Info("{:c}", 300000));

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[error] formato de log inválido: {:c}"}));
}

TEST(LogTest, BelowTheMinimumIsNotFormatted)
{
    RecordingSink sink;
    Log quiet(sink, LogLevel::Info);

    EXPECT_NO_THROW(quiet.Debug("{:c}", 300000));

    // Si se formateara antes de mirar el mínimo, el error de formato dejaría una línea.
    EXPECT_TRUE(sink.lines.empty());
}

TEST(LogTest, StderrSinkWritesOneLinePerMessage)
{
    const std::unique_ptr<std::FILE, FileCloser> output(std::tmpfile());
    ASSERT_NE(output, nullptr);
    StderrSink sink(output.get());
    Log log(sink, LogLevel::Info);

    log.Info("uno");
    log.Error("dos {}", 2);

    std::rewind(output.get());
    std::string text;
    for (int c = std::fgetc(output.get()); c != EOF; c = std::fgetc(output.get()))
    {
        text += static_cast<char>(c);
    }
    EXPECT_EQ(text, "[info] uno\n[error] dos 2\n");
}

TEST(LogTest, FileSinkWritesOneLinePerMessage)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / (u8"carpeta-\u00f1") / "muret-client.log";
    fs::create_directories(path.parent_path());

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    log.Info("uno");
    log.Warning("dos {}", 2);

    // Se lee con el FileSink todavía abierto: cada línea ya está en el disco (el flush).
    EXPECT_EQ(ReadText(path), "[info] uno\n[warning] dos 2\n");
}

TEST(LogTest, FileSinkAppendsToThePreviousRun)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / "muret-client.log";
    std::ofstream(path) << "[info] de la corrida anterior\n";

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    log.Info("de esta");

    EXPECT_EQ(ReadText(path), "[info] de la corrida anterior\n[info] de esta\n");
}

TEST(LogTest, FileSinkEmptiesAFileOverTheCap)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / "muret-client.log";
    std::ofstream(path) << std::string(MaxLogFileBytes + 1, 'x');

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    log.Info("de esta");

    EXPECT_EQ(ReadText(path), "[info] de esta\n");
}

TEST(LogTest, FileSinkKeepsLinesWholeFromTwoThreads)
{
    constexpr int LinesPerThread = 1000; // bastantes para que los dos hilos se crucen
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / "muret-client.log";
    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    const auto writeLines = [&log](int thread)
    {
        for (int i = 0; i < LinesPerThread; ++i)
        {
            log.Info("hilo {} línea {}", thread, i);
        }
    };
    std::thread first(writeLines, 1);
    std::thread second(writeLines, 2);
    first.join();
    second.join();

    std::vector<std::string> expected;
    for (int thread = 1; thread <= 2; ++thread)
    {
        for (int i = 0; i < LinesPerThread; ++i)
        {
            expected.push_back(std::format("[info] hilo {} línea {}", thread, i));
        }
    }
    std::vector<std::string> lines = ReadLines(path);
    std::ranges::sort(lines);
    std::ranges::sort(expected);
    EXPECT_EQ(lines, expected);
}

TEST(LogTest, FileSinkNamesAPathThatDoesNotOpen)
{
    const TemporaryFolder folder;
    const fs::path path = folder.Path() / (u8"no-existe-\u00f1") / "muret-client.log";

    const auto opened = FileSink::Open(path);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("no-existe-" + std::string(EnyeUtf8)), std::string::npos) << opened.Error();
    EXPECT_NE(opened.Error().find(PathText(path)), std::string::npos) << opened.Error();
}
} // namespace
