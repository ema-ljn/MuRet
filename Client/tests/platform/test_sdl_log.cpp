#include "Platform/SdlLog.h"

#include <gtest/gtest.h>

#include <SDL3/SDL_log.h>

#include <string>
#include <string_view>
#include <vector>

// Client::Platform::SdlLog: los mensajes de SDL llegan al Core::Log con su nivel, y el destructor
// devuelve la salida por defecto. Sin SDL_Init: SDL_Log no lo necesita.
namespace
{
using Client::Core::ILogSink;
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Platform::LevelOf;
using Client::Platform::SdlLog;

// Guarda cada línea que le llega. Se usa desde un solo hilo: no necesita mutex.
class RecordingSink final : public ILogSink
{
public:
    void Write(LogLevel /*level*/, std::string_view line) override
    {
        lines.emplace_back(line);
    }

    std::vector<std::string> lines;
};

// Deja pasar todas las prioridades mientras dura el test (SDL filtra antes de llamar a la salida:
// por defecto, de INFO para arriba en la categoría de la aplicación) y las vuelve a las de SDL.
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

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[debug] detalle", "[info] con {} llaves", "[warning] aviso",
                                                    "[error] grave"}));
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
    SDL_Log("%s", "este va a la salida por defecto de SDL");
    EXPECT_TRUE(sink.lines.empty());
}
} // namespace
