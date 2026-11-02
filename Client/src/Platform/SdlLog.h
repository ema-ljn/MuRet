#pragma once

#include "Core/Log.h"

#include <SDL3/SDL_log.h>

// Los mensajes de SDL (SDL_Log y los de sus subsistemas) al Core::Log del cliente, en vez de a la
// salida por defecto de SDL. Va en main después del Log, así se destruye antes que él.
namespace Client::Platform
{
// El nivel del log para la prioridad de un mensaje de SDL: TRACE, VERBOSE y DEBUG van a Debug;
// INFO a Info; WARN a Warning; ERROR y CRITICAL a Error. INVALID y COUNT no son prioridades de un
// mensaje, y van a Error para que se vean.
[[nodiscard]] Core::LogLevel LevelOf(SDL_LogPriority priority);

// RAII: el constructor pone la salida de SDL con SDL_SetLogOutputFunction(callback, &log), y el
// destructor vuelve a SDL_GetDefaultLogOutputFunction(). SDL llama al callback desde cualquier
// hilo, pero con su propio mutex tomado (nunca dos a la vez); el Log y sus salidas aguantan hilos
// igual (Core/Log.h). El texto de SDL va con Log::Write: sus llaves no son un formato.
//
// Uno solo a la vez: la salida de SDL es una sola para todo el proceso, y el segundo SdlLog le
// sacaría la suya al primero.
class SdlLog
{
public:
    explicit SdlLog(Core::Log& log);
    ~SdlLog();

    SdlLog(const SdlLog&) = delete;
    SdlLog& operator=(const SdlLog&) = delete;
    SdlLog(SdlLog&&) = delete;
    SdlLog& operator=(SdlLog&&) = delete;
};
} // namespace Client::Platform
