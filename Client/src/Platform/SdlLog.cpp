#include "Platform/SdlLog.h"

#include <cstdio>
#include <string_view>

namespace Client::Platform
{
namespace
{
// La salida que SDL llama. SDL es C: una excepción (un bad_alloc al armar la línea) que cruzara
// sus frames es comportamiento indefinido, así que no sale ninguna. La categoría no se usa.
void SDLCALL WriteToLog(void* userdata, int /*category*/, SDL_LogPriority priority, const char* message) noexcept
{
    try
    {
        auto& log = *static_cast<Core::Log*>(userdata);
        log.Write(LevelOf(priority), message != nullptr ? std::string_view(message) : std::string_view());
    }
    catch (...)
    {
        // El mensaje se pierde; queda el aviso en stderr, que no tira.
        std::fputs("[error] no se pudo escribir un mensaje de SDL en el log\n", stderr);
    }
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
    SDL_SetLogOutputFunction(WriteToLog, &log);
}

SdlLog::~SdlLog()
{
    // SDL pide NULL de userdata con la salida por defecto.
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
}
} // namespace Client::Platform
