#pragma once

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "PlatformApi.h"

#include <SDL3/SDL_log.h>

// Instead of SDL's default output. With SDL's default priorities only ERROR passes from its subsystems (APPLICATION
// lets INFO and up through, ASSERT WARN and up): SDL filters before calling the output. MuRet does not chain to the
// previous output (MuMain does, in SdlLogBridge): it sets its own, restores the default on destruction and filters
// once, with the Core::Log minimum.
namespace Client::Platform::Sdl
{
// INVALID and COUNT are not message priorities; they go to Error so they are seen.
[[nodiscard]] PLATFORM_API Core::LogLevel LevelOf(SDL_LogPriority priority);

// SDL's text goes through Log::Write (its braces are not a format), split into lines without "\r" or empty lines, like
// those SDL_assert's report leaves. SDL calls the callback from any thread but with its log mutex held, and
// SDL_SetLogOutputFunction takes the same mutex (SDL_log.c:831-838): once ~SdlLog returns no callback runs with &log,
// and the Log can die right after. That mutex exists from the first call that initializes SDL's log (the constructor
// makes it) until SDL_Quit destroys it. SDL_Quit (SDL_QuitLog) does not restore the default output: in main the SdlLog
// dies before the SdlContext, and nobody logs in between. SDL's mutex is recursive: if a Log sink makes SDL log on the
// same thread the callback re-enters; that line goes straight to stderr, not to the Log, which would hang on its
// sink's std::mutex (Core/ILogSink.h). One at a time: SDL's output is one per process, and a second SdlLog would take
// the first's away (an assert checks it in Debug). The Log must outlive it.
class PLATFORM_API SdlLog
{
public:
    explicit SdlLog(Core::Log& log);
    ~SdlLog();

    SdlLog(const SdlLog&) = delete;
    SdlLog& operator=(const SdlLog&) = delete;
    SdlLog(SdlLog&&) = delete;
    SdlLog& operator=(SdlLog&&) = delete;
};
} // namespace Client::Platform::Sdl
