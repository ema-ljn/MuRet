#include "Core/FileSink.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PathText.h"
#include "Core/StderrSink.h"
#include "Core/TeeSink.h"
#include "support/TemporaryFile.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <signal.h>
#include <sys/resource.h>
#endif

namespace
{
namespace fs = std::filesystem;
using Client::Core::FileSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::MaxLogFileBytes;
using Client::Core::PathText;
using Client::Core::StderrSink;
using Client::Core::TeeSink;
using Client::Tests::OpenTemporaryFile;
using Client::Tests::ReadAll;
using Client::Tests::TemporaryFolder;

// For non-ASCII paths (PathText and, on Windows, the wide path used to open).
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

    // Read with the FileSink still open: each line is already on disk (the flush).
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

// Q-C48 (07-quirks-y-bugs): two sinks must append without overwriting each other. The one that emptied on open must
// also append after what the other wrote; two threads of the same sink do not test that.
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

// Q-C48: the cap must be exceeded; reaching it exactly does not erase the previous run's log.
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
    constexpr int LinesPerThread = 1000; // enough for the two threads to interleave
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

// A file that opens and fails on every write, like a full disk: /dev/full gives ENOSPC. Windows has no such file, so
// the tests that use it are skipped there (AUD-C02 is tested on Linux only).
std::optional<fs::path> FullDevice()
{
#ifdef _WIN32
    return std::nullopt;
#else
    std::error_code error;
    const fs::path path = "/dev/full";
    if (!fs::exists(path, error))
    {
        return std::nullopt;
    }
    return path;
#endif
}

// With the path as PathText writes it.
std::string FailureNotice(const fs::path& path)
{
    return "[warning] no se pudo escribir en el archivo de log " + PathText(path) + ": el log sigue solo en stderr\n";
}

TEST(FileSinkTest, AWriteFailureCanBeObserved)
{
    const std::optional<fs::path> full = FullDevice();
    if (!full)
    {
        GTEST_SKIP() << "sin /dev/full en este sistema";
    }
    const auto notice = OpenTemporaryFile();
    ASSERT_NE(notice, nullptr);
    const auto opened = FileSink::Open(*full, notice.get());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();

    opened.Value()->Write(LogLevel::Info, "[info] uno");

    EXPECT_EQ(ReadAll(notice.get()), FailureNotice(*full));
}

TEST(FileSinkTest, ReportsAWriteFailureOnce)
{
    const std::optional<fs::path> full = FullDevice();
    if (!full)
    {
        GTEST_SKIP() << "sin /dev/full en este sistema";
    }
    const auto notice = OpenTemporaryFile();
    ASSERT_NE(notice, nullptr);
    const auto opened = FileSink::Open(*full, notice.get());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();

    for (int i = 0; i < 3; ++i)
    {
        opened.Value()->Write(LogLevel::Info, std::format("[info] línea {}", i));
    }

    EXPECT_EQ(ReadAll(notice.get()), FailureNotice(*full));
}

// As in main. With the file broken, stderr still gets every line, and the notice goes on its own, without re-entering
// the Log or the Tee.
TEST(FileSinkTest, StderrKeepsTheLinesAfterTheFileFails)
{
    const std::optional<fs::path> full = FullDevice();
    if (!full)
    {
        GTEST_SKIP() << "sin /dev/full en este sistema";
    }
    const auto errorOutput = OpenTemporaryFile();
    const auto notice = OpenTemporaryFile();
    ASSERT_NE(errorOutput, nullptr);
    ASSERT_NE(notice, nullptr);
    const auto opened = FileSink::Open(*full, notice.get());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    StderrSink stderrSink(errorOutput.get());
    TeeSink tee(stderrSink, *opened.Value());
    Log log(tee, LogLevel::Info);

    log.Info("uno");
    log.Warning("dos");
    log.Error("tres");

    EXPECT_EQ(ReadAll(errorOutput.get()), "[info] uno\n[warning] dos\n[error] tres\n");
    EXPECT_EQ(ReadAll(notice.get()), FailureNotice(*full));
}

#ifndef _WIN32
// While it lives, no file of the process grows past `bytes`: writing past gives EFBIG. The signal the system sends
// (SIGXFSZ) would end the test, so it is ignored. Unlike /dev/full, the file works again once the cap is lifted, which
// shows whether FileSink retries.
class FileSizeLimit
{
public:
    explicit FileSizeLimit(rlim_t bytes)
    {
        m_previousHandler = std::signal(SIGXFSZ, SIG_IGN);
        m_saved = getrlimit(RLIMIT_FSIZE, &m_previous) == 0;
        rlimit limit = m_previous;
        limit.rlim_cur = bytes;
        m_active = m_saved && m_previousHandler != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0;
    }

    ~FileSizeLimit()
    {
        if (m_saved)
        {
            setrlimit(RLIMIT_FSIZE, &m_previous);
        }
        if (m_previousHandler != SIG_ERR)
        {
            std::signal(SIGXFSZ, m_previousHandler);
        }
    }

    FileSizeLimit(const FileSizeLimit&) = delete;
    FileSizeLimit& operator=(const FileSizeLimit&) = delete;

    [[nodiscard]] bool Active() const
    {
        return m_active;
    }

private:
    using SignalHandler = void (*)(int);

    SignalHandler m_previousHandler = SIG_ERR;
    rlimit m_previous{};
    bool m_saved = false;
    bool m_active = false;
};
#endif

// After the failure nothing more reaches the file, even with room again: neither the next lines nor, on close, the one
// that failed, which libstdc++'s ofstream keeps in its buffer and its destructor sends again.
TEST(FileSinkTest, WritesNothingAfterAFailure)
{
#ifdef _WIN32
    GTEST_SKIP() << "sin RLIMIT_FSIZE en Windows";
#else
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    // Another run's content, longer than the notice: the cap applies to every file of the process, the notice's
    // temporary file too.
    const std::string previous = std::string(1023, 'x') + '\n';
    ASSERT_LT(FailureNotice(path).size(), previous.size());
    std::ofstream(path) << previous;
    const auto notice = OpenTemporaryFile();
    ASSERT_NE(notice, nullptr);
    {
        const auto opened = FileSink::Open(path, notice.get());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        {
            const FileSizeLimit limit(static_cast<rlim_t>(previous.size()));
            ASSERT_TRUE(limit.Active());
            opened.Value()->Write(LogLevel::Info, "[info] uno");
        }
        opened.Value()->Write(LogLevel::Info, "[info] dos");
    }

    EXPECT_EQ(ReadText(path), previous);
    EXPECT_EQ(ReadAll(notice.get()), FailureNotice(path));
#endif
}

TEST(FileSinkTest, AWorkingFileGivesNoNotice)
{
    const TemporaryFolder folder("file-sink");
    const fs::path path = folder.Path() / "muret-client.log";
    const auto notice = OpenTemporaryFile();
    ASSERT_NE(notice, nullptr);
    {
        const auto opened = FileSink::Open(path, notice.get());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        opened.Value()->Write(LogLevel::Info, "[info] uno");
    }

    EXPECT_EQ(ReadText(path), "[info] uno\n");
    EXPECT_EQ(ReadAll(notice.get()), "");
}
} // namespace
