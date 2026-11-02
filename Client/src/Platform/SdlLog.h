#pragma once

#include "Core/Log.h"

#include <SDL3/SDL_log.h>

// Los mensajes de SDL (SDL_Log y los de sus subsistemas) al Core::Log del cliente, en vez de a la
// salida por defecto de SDL. Cada línea va con "SDL: " adelante. Con las prioridades por defecto
// de SDL, de sus subsistemas solo pasa ERROR (APPLICATION deja pasar desde INFO y ASSERT desde
// WARN): SDL filtra antes de llamar a la salida.
//
// MuRet no encadena a la salida que hubiera antes (MuMain sí, en SdlLogBridge): pone la suya, al
// morir restaura la de por defecto, y filtra una sola vez, con el mínimo del Core::Log.
namespace Client::Platform
{
// El nivel del log para la prioridad de un mensaje de SDL: TRACE, VERBOSE y DEBUG van a Debug;
// INFO a Info; WARN a Warning; ERROR y CRITICAL a Error. INVALID y COUNT no son prioridades de un
// mensaje, y van a Error para que se vean.
[[nodiscard]] Core::LogLevel LevelOf(SDL_LogPriority priority);

// RAII: el constructor pone la salida de SDL con SDL_SetLogOutputFunction(callback, &log), y el
// destructor vuelve a SDL_GetDefaultLogOutputFunction(). El texto de SDL va con Log::Write (sus
// llaves no son un formato), partido en líneas: sin los "\r" y sin las vacías, como las que deja
// el reporte de SDL_assert.
//
// Los hilos. SDL llama al callback desde cualquier hilo, pero con su mutex del log tomado (nunca
// dos a la vez), y SDL_SetLogOutputFunction toma el mismo mutex (SDL_log.c:831-838): cuando vuelve
// ~SdlLog no queda ningún callback corriendo con &log, y el Log puede morir justo después. Ese
// mutex existe entre la primera llamada que inicializa el log de SDL (el constructor la hace) y
// SDL_Quit, que lo destruye. SDL_Quit (SDL_QuitLog) no devuelve la salida por defecto: en main el
// SdlLog muere antes que el SdlContext, y nadie loguea entre SDL_Quit y la muerte del SdlLog.
//
// El mutex de SDL es recursivo: si una salida del Log hace que SDL loguee en el mismo hilo, el
// callback vuelve a entrar. Esa línea va directo a stderr y no al Log, que se trabaría en el
// std::mutex de su salida (Core/ILogSink.h).
//
// Uno solo a la vez: la salida de SDL es una sola para todo el proceso, y el segundo SdlLog le
// sacaría la suya al primero (un assert lo mira en Debug). El Log tiene que vivir más que él.
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
