#include "Core/FileSink.h"

#include "Core/Log.h"
#include "Core/PathText.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Client::Core::FileSink: agregar, el tope, aperturas independientes, dos hilos y una ruta que no
// abre, en una carpeta temporal.
namespace
{
namespace fs = std::filesystem;
using Client::Core::FileSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::MaxLogFileBytes;
using Client::Core::PathText;
using Client::Tests::TemporaryFolder;

// "ñ" en UTF-8, para las rutas que no son ASCII (PathText y, en Windows, la ruta ancha de abrir).
constexpr std::string_view EnyeUtf8 = "\xC3\xB1";

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

TEST(FileSinkTest, WritesOneLinePerMessage)
{
    const TemporaryFolder folder("file-sink");
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

TEST(FileSinkTest, AppendsToThePreviousRun)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    std::ofstream(path) << "[info] de la corrida anterior\n";

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    log.Info("de esta");

    EXPECT_EQ(ReadText(path), "[info] de la corrida anterior\n[info] de esta\n");
}

TEST(FileSinkTest, EmptiesAFileOverTheCap)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    std::ofstream(path) << std::string(MaxLogFileBytes + 1, 'x');

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Log log(*opened.Value(), LogLevel::Info);

    log.Info("de esta");

    EXPECT_EQ(ReadText(path), "[info] de esta\n");
}

// Q-C48 (07-quirks-y-bugs): dos sinks deben agregar sin pisarse. El que vació al abrir también
// tiene que agregar después de lo que escribió el otro; dos hilos del mismo sink no prueban eso.
TEST(FileSinkTest, AppendsFromIndependentSinksAfterTheCap)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    auto first = FileSink::Open(path);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    first.Value()->Write(LogLevel::Info, std::string(MaxLogFileBytes + 1, 'x'));
    ASSERT_GT(fs::file_size(path), MaxLogFileBytes);

    auto second = FileSink::Open(path);
    ASSERT_TRUE(second.HasValue()) << second.Error();
    second.Value()->Write(LogLevel::Info, "BBB");
    first.Value()->Write(LogLevel::Info, "AAAA");
    second.Value()->Write(LogLevel::Info, "CCC");

    EXPECT_EQ(ReadText(path), "BBB\nAAAA\nCCC\n");
}

// Q-C48: el tope se supera; alcanzarlo exactamente no borra el log de la corrida anterior.
TEST(FileSinkTest, KeepsAFileExactlyAtTheCap)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    const std::string previous(MaxLogFileBytes, 'x');
    std::ofstream(path) << previous;
    ASSERT_EQ(fs::file_size(path), MaxLogFileBytes);

    auto opened = FileSink::Open(path);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    opened.Value()->Write(LogLevel::Info, "nueva");

    EXPECT_EQ(ReadText(path), previous + "nueva\n");
}

TEST(FileSinkTest, KeepsLinesWholeFromTwoThreads)
{
    constexpr int LinesPerThread = 1000; // bastantes para que los dos hilos se crucen
    const TemporaryFolder folder("file-sink");
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

TEST(FileSinkTest, NamesAPathThatDoesNotOpen)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / (u8"no-existe-\u00f1") / "muret-client.log";

    const auto opened = FileSink::Open(path);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("no-existe-" + std::string(EnyeUtf8)), std::string::npos) << opened.Error();
    EXPECT_NE(opened.Error().find(PathText(path)), std::string::npos) << opened.Error();
}
} // namespace
