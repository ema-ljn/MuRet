#pragma once

#include "Core/ILogSink.h"

#include <format>
#include <string_view>

// El log del cliente: cuatro niveles, una línea por mensaje, y la salida aparte (ILogSink), así un
// test lee lo que se escribió sin tocar stderr ni el disco.
//
//   Core::StderrSink errorOutput;
//   Core::Log log(errorOutput, Core::LogLevel::Info);
//   log.Warning("un frame de {} ms", 1200); // escribe "[warning] un frame de 1200 ms"
//
// Propio y no spdlog (pregunta 4 del plan de la CL3): hacen falta cuatro niveles, stderr, un
// archivo, una salida de prueba y un mutex, y eso son pocas líneas con std::format; spdlog trae
// fmt. Si la CL4 pide más (rotación, varios hilos propios), se revisa.
//
// Sin globales (D-C1): el Log lo crea main.cpp y se pasa por referencia a quien lo use. Su salida
// tiene que vivir más que él.
//
// Los hilos. SDL puede loguear desde sus hilos (SDL_Log es seguro entre hilos), así que dos hilos
// pueden escribir al mismo Log a la vez. El Log no guarda nada que cambie: arma la línea en un
// local y se la da a su salida. Por eso el mutex no va en el Log sino en cada salida que escribe en
// algo compartido (StderrSink y FileSink, como los sinks _mt de spdlog): así un Log que escribe
// directo a stderr, sin un TeeSink porque el archivo no abrió, tampoco mezcla dos líneas. El
// TeeSink tiene el suyo para el orden (TeeSink.h), y ningún Write puede loguear (ILogSink.h).
//
// Una línea por mensaje: el Log no mira el texto, así que un "\n" adentro de un mensaje da dos
// líneas en la salida, y la segunda sin prefijo.
namespace Client::Core
{
// El prefijo de cada línea: "[info] ", "[warning] "... El de un valor que no es ninguno de los
// cuatro es el de error. Lo usa también el manejador de caídas, que escribe su línea sin el Log
// (Crash::CrashLogLine).
[[nodiscard]] std::string_view LevelPrefix(LogLevel level);

class Log
{
public:
    // Escribe en `sink` los mensajes de `minimum` para arriba.
    Log(ILogSink& sink, LogLevel minimum);

    // La línea es el prefijo del nivel y el texto: Info("hola {}", 1) escribe "[info] hola 1". El
    // formato se revisa al compilar (std::format_string), y un mensaje que no llega al mínimo no
    // se formatea. Lo que solo falla al formatear ({:c} con un número que no es una letra) no
    // sale del Log: escribe "[error] formato de log inválido: " y el formato.
    template <typename... Args> void Debug(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Debug, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Info(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Info, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Warning(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Warning, format.get(), std::make_format_args(args...));
    }

    template <typename... Args> void Error(std::format_string<Args...> format, Args&&... args)
    {
        WriteFormatted(LogLevel::Error, format.get(), std::make_format_args(args...));
    }

    // Con el nivel elegido mientras corre, como la prioridad de un mensaje de SDL (SdlLog), sin un
    // switch sobre los cuatro de arriba. El texto ya viene armado y no se formatea: las llaves que
    // traiga se escriben tal cual.
    void Write(LogLevel level, std::string_view message);

private:
    // El cuerpo de los cuatro de arriba, fuera del template: std::vformat con los argumentos ya
    // empaquetados. `arguments` apunta a los de quien llama, así que se usa solo durante la
    // llamada.
    void WriteFormatted(LogLevel level, std::string_view format, std::format_args arguments);

    ILogSink& m_sink;
    LogLevel m_minimum;
};
} // namespace Client::Core
