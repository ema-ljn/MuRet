#include "Platform/SdlLog.h"

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

namespace Client::Platform
{
namespace
{
// Si hay un SdlLog vivo (uno solo a la vez, SdlLog.h).
std::atomic<bool> s_installed = false;

// Si este hilo ya está adentro del callback: una salida del Log hizo loguear a SDL (SdlLog.h).
thread_local bool s_writing = false;

// Cada línea del mensaje como una línea del Log, con "SDL: " adelante y el mismo nivel.
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

// La salida que SDL llama. SDL es C: una excepción (un bad_alloc al armar la línea, o una que
// tire una salida) que cruzara sus frames es comportamiento indefinido, así que no sale ninguna.
// La categoría no se usa.
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
        // El mensaje se pierde; queda el aviso en stderr, que no tira.
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
    // Un valor que no está en el enum (un número pasado a SDL_LogMessage): como los de arriba.
    return Core::LogLevel::Error;
}

SdlLog::SdlLog(Core::Log& log)
{
    [[maybe_unused]] const bool another = s_installed.exchange(true);
    assert(!another && "un solo SdlLog a la vez");
    // SDL_SetLogOutputFunction no inicializa el log de SDL, y sin eso su mutex no existe todavía
    // (lo crea SDL_InitLog, que llaman las funciones de prioridad): así queda creado antes.
    static_cast<void>(SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION));
    SDL_SetLogOutputFunction(WriteToLog, &log);
}

SdlLog::~SdlLog()
{
    // SDL pide NULL de userdata con la salida por defecto.
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
    s_installed = false;
}
} // namespace Client::Platform
