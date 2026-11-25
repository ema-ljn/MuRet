#pragma once

#include <cstdint>
#include <string_view>

// Adónde va una línea del log (Core::Log). Las salidas son TeeSink, StderrSink y FileSink, y los
// tests tienen la suya, que guarda las líneas.
namespace Client::Core
{
enum class LogLevel : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error
};

class ILogSink
{
public:
    virtual ~ILogSink() = default;

    // `line` ya trae el prefijo del nivel ("[info] ") y no trae el fin de línea: lo pone la salida.
    //
    // Puede llegar desde varios hilos a la vez (SDL loguea desde los suyos), así que una salida
    // que escribe en algo compartido lo protege con su std::mutex. Y como ese mutex no es
    // recursivo, un Write no puede loguear ni llamar a SDL (que puede loguear): volvería a entrar
    // a su propio mutex, o al del TeeSink que la llamó, y el hilo se trabaría.
    virtual void Write(LogLevel level, std::string_view line) = 0;

protected:
    // Como IObfuscation: se copia y se mueve solo desde una clase hija, para que asignar a través
    // de dos ILogSink& no copie solo la parte de la base (slicing).
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
    ILogSink(ILogSink&&) = default;
    ILogSink& operator=(ILogSink&&) = default;
};
} // namespace Client::Core
