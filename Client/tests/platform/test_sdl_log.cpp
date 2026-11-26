#include "Platform/Sdl/SdlLog.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <SDL3/SDL_log.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Without SDL_Init: SDL_Log does not need it.
namespace
{
using Client::Core::ILogSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Platform::Sdl::LevelOf;
using Client::Platform::Sdl::SdlLog;
using Client::Tests::RecordingSink;

// Lets every priority through while the test lasts (SDL filters before calling the output: by default, INFO and up in
// the application category) and then restores SDL's.
class AllSdlPriorities
{
public:
    AllSdlPriorities()
    {
        SDL_SetLogPriorities(SDL_LOG_PRIORITY_TRACE);
    }

    ~AllSdlPriorities()
    {
        SDL_ResetLogPriorities();
    }

    AllSdlPriorities(const AllSdlPriorities&) = delete;
    AllSdlPriorities& operator=(const AllSdlPriorities&) = delete;
};

class ThrowingSink final : public ILogSink
{
public:
    void Write(LogLevel /*level*/, std::string_view /*line*/) override
    {
        throw std::runtime_error("la salida falló");
    }
};

// On its first line, it makes SDL log from the same thread.
class ReentrantSink final : public ILogSink
{
public:
    void Write(LogLevel level, std::string_view line) override
    {
        if (!m_logged)
        {
            m_logged = true;
            SDL_Log("%s", "desde adentro de la salida");
        }
        recorded.Write(level, line);
    }

    RecordingSink recorded;

private:
    bool m_logged = false;
};

TEST(SdlLogTest, MapsEachPriority)
{
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_TRACE), LogLevel::Debug);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_VERBOSE), LogLevel::Debug);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_DEBUG), LogLevel::Debug);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_INFO), LogLevel::Info);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_WARN), LogLevel::Warning);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_ERROR), LogLevel::Error);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_CRITICAL), LogLevel::Error);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_INVALID), LogLevel::Error);
    EXPECT_EQ(LevelOf(SDL_LOG_PRIORITY_COUNT), LogLevel::Error);
}

TEST(SdlLogTest, SdlMessagesReachTheLog)
{
    const AllSdlPriorities allPriorities;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const SdlLog sdlLog(log);

    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE, "%s", "detalle");
    SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO, "%s", "con {} llaves");
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN, "%s", "aviso");
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL, "%s", "grave");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[debug] SDL: detalle", "[info] SDL: con {} llaves",
                                                    "[warning] SDL: aviso", "[error] SDL: grave"}));
}

TEST(SdlLogTest, SplitsTheLinesOfAMessage)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    const SdlLog sdlLog(log);

    // SDL_assert's report goes with "\n\n%s\n\n"; SDL trims only the last line end.
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR, "%s", "\n\nx\n\n");
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR, "%s", "uno\r\ndos");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[error] SDL: x", "[error] SDL: uno", "[error] SDL: dos"}));
    EXPECT_EQ(sink.levels, (std::vector<LogLevel>(3, LogLevel::Error)));
}

TEST(SdlLogTest, AnExceptionInTheSinkDoesNotCrossSdl)
{
    ThrowingSink sink;
    Log log(sink, LogLevel::Debug);
    const SdlLog sdlLog(log);

    // If the exception crossed the noexcept callback, the process would end here.
    SDL_Log("%s", "a una salida que tira");

    SUCCEED();
}

TEST(SdlLogTest, AReentrantMessageGoesToStderr)
{
    ReentrantSink sink;
    Log log(sink, LogLevel::Debug);
    const SdlLog sdlLog(log);

    SDL_Log("%s", "afuera");

    // The inner one went to stderr: only the outer one reached the Log, and nothing hung.
    EXPECT_EQ(sink.recorded.lines, (std::vector<std::string>{"[info] SDL: afuera"}));
}

TEST(SdlLogTest, TheDestructorRestoresTheDefaultOutput)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    SDL_LogOutputFunction output = nullptr;
    void* userdata = nullptr;
    {
        const SdlLog sdlLog(log);
        SDL_GetLogOutputFunction(&output, &userdata);
        EXPECT_NE(output, SDL_GetDefaultLogOutputFunction());
        EXPECT_EQ(userdata, &log);
    }

    SDL_GetLogOutputFunction(&output, &userdata);
    EXPECT_EQ(output, SDL_GetDefaultLogOutputFunction());
    EXPECT_EQ(userdata, nullptr);
}
} // namespace
