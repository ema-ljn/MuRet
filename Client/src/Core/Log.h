#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
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
// Sin globales (D-C1): el Log lo crea main.cpp y se pasa por referencia a quien lo use. Las salidas
// tienen que vivir más que el Log que escribe en ellas.
//
// Los hilos. SDL puede loguear desde sus hilos (SDL_Log es seguro entre hilos), así que dos hilos
// pueden escribir al mismo Log a la vez. El Log no guarda nada que cambie: arma la línea en un
// local y se la da a su salida. Por eso el mutex no va en el Log sino en cada salida que escribe en
// algo compartido (StderrSink y FileSink, como los sinks _mt de spdlog): así un Log que escribe
// directo a stderr, sin un TeeSink porque el archivo no abrió, tampoco mezcla dos líneas. El
// TeeSink tiene el suyo para que las dos salidas reciban las líneas en el mismo orden. Los
// mutex se toman siempre en ese orden (el del Tee y después el de cada salida), así que no se
// traban. Una salida de un test que se usa desde un solo hilo no necesita ninguno.
namespace Client::Core
{
enum class LogLevel : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error
};

// Adónde va una línea. `line` ya trae el prefijo del nivel ("[info] ") y no trae el fin de línea:
// lo pone la salida. Write puede llegar desde varios hilos a la vez (ver arriba).
class ILogSink
{
public:
    virtual ~ILogSink() = default;

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

// Escribe la misma línea en las dos salidas (stderr y el archivo, en main.cpp), con un std::mutex
// para que las dos las reciban en el mismo orden aunque lleguen de dos hilos.
class TeeSink final : public ILogSink
{
public:
    TeeSink(ILogSink& first, ILogSink& second);

    void Write(LogLevel level, std::string_view line) override;

private:
    ILogSink& m_first;
    ILogSink& m_second;
    std::mutex m_mutex;
};

// Cada línea a stderr, con su fin de línea. stderr no tiene buffer: la línea sale al escribirla.
class StderrSink final : public ILogSink
{
public:
    void Write(LogLevel level, std::string_view line) override;

private:
    std::mutex m_mutex;
};

// Cada línea a un archivo de texto, con su fin de línea y un flush: si el cliente se cae, lo que
// se logueó hasta ahí ya está en el disco. Abrir el archivo lo vacía, así guarda solo la última
// corrida.
class FileSink final : public ILogSink
{
public:
    // Si no se puede abrir (la carpeta no existe, no hay permiso), un error que nombra la ruta:
    // main lo avisa por stderr y sigue sin archivo. Va en un unique_ptr porque el mutex no se
    // mueve, y el Expected guarda su valor moviéndolo.
    [[nodiscard]] static Expected<std::unique_ptr<FileSink>, std::string> Open(const std::filesystem::path& path);

    // Toma un archivo ya abierto; lo normal es pedirlo con Open, que mira que haya abierto.
    explicit FileSink(std::ofstream file);

    void Write(LogLevel level, std::string_view line) override;

private:
    std::ofstream m_file;
    std::mutex m_mutex;
};

class Log
{
public:
    // Escribe en `sink` los mensajes de `minimum` para arriba.
    Log(ILogSink& sink, LogLevel minimum);

    // La línea es el prefijo del nivel y el texto: Info("hola {}", 1) escribe "[info] hola 1". El
    // formato se revisa al compilar (std::format_string), y un mensaje que no llega al mínimo no
    // se formatea.
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

    // Un texto que ya viene armado y que no se formatea, como los mensajes de SDL (SdlLog): las
    // llaves que traiga se escriben tal cual.
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
