#include "Platform/Sdl/SdlLog.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Platform::Sdl
{
namespace
{
// Only one at a time (SdlLog.h).
std::atomic<bool> s_installed = false;

// This thread is already inside the callback: a Log sink made SDL log (SdlLog.h).
thread_local bool s_writing = false;

void WriteLines(Core::Log& log, Core::LogLevel level, std::string_view message)
{
    while (!message.empty())
    {
        const std::size_t end = message.find('\n');
        std::string line(message.substr(0, end));
        std::erase(line, '\r');
        if (!line.empty())
        {
            log.Write(level, "SDL: " + line);
        }
        message = end == std::string_view::npos ? std::string_view() : message.substr(end + 1);
    }
}

// SDL is C: an exception (a bad_alloc building the line, or one a sink throws) crossing its frames is undefined
// behavior, so none escapes. The category is unused.
void SDLCALL WriteToLog(void* userdata, int /*category*/, SDL_LogPriority priority, const char* message) noexcept
{
    if (message == nullptr)
    {
        return;
    }
    if (s_writing)
    {
        std::fputs("SDL: ", stderr);
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
        return;
    }
    s_writing = true;
    try
    {
        WriteLines(*static_cast<Core::Log*>(userdata), LevelOf(priority), message);
    }
    catch (...)
    {
        // The message is lost; the notice on stderr remains, which does not throw.
        std::fputs("[error] no se pudo escribir un mensaje de SDL en el log\n", stderr);
    }
    s_writing = false;
}
} // namespace

Core::LogLevel LevelOf(SDL_LogPriority priority)
{
    switch (priority)
    {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE:
    case SDL_LOG_PRIORITY_DEBUG:
        return Core::LogLevel::Debug;
    case SDL_LOG_PRIORITY_INFO:
        return Core::LogLevel::Info;
    case SDL_LOG_PRIORITY_WARN:
        return Core::LogLevel::Warning;
    case SDL_LOG_PRIORITY_ERROR:
    case SDL_LOG_PRIORITY_CRITICAL:
        return Core::LogLevel::Error;
    case SDL_LOG_PRIORITY_INVALID:
    case SDL_LOG_PRIORITY_COUNT:
        return Core::LogLevel::Error;
    }
    // A value outside the enum (a number passed to SDL_LogMessage): like the ones above.
    return Core::LogLevel::Error;
}

SdlLog::SdlLog(Core::Log& log)
{
    [[maybe_unused]] const bool another = s_installed.exchange(true);
    assert(!another && "un solo SdlLog a la vez");
    // SDL_SetLogOutputFunction does not initialize SDL's log, and without that its mutex does not exist yet
    // (SDL_InitLog creates it, called by the priority functions): this creates it first.
    static_cast<void>(SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION));
    SDL_SetLogOutputFunction(WriteToLog, &log);
}

SdlLog::~SdlLog()
{
    // SDL wants NULL userdata with the default output.
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
    s_installed = false;
}
} // namespace Client::Platform::Sdl
