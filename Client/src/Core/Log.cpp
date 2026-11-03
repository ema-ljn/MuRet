#include "Core/Log.h"

#include "Core/ILogSink.h"

#include <format>
#include <string>
#include <string_view>

namespace Client::Core
{
namespace
{
std::string_view PrefixOf(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return "[debug] ";
    case LogLevel::Info:
        return "[info] ";
    case LogLevel::Warning:
        return "[warning] ";
    case LogLevel::Error:
        return "[error] ";
    }
    // Un valor que no es ninguno de los cuatro (un static_cast de un número): se escribe como
    // error, que es lo que más se ve.
    return "[error] ";
}
} // namespace

Log::Log(ILogSink& sink, LogLevel minimum) : m_sink(sink), m_minimum(minimum) {}

void Log::Write(LogLevel level, std::string_view message)
{
    if (level < m_minimum)
    {
        return;
    }
    std::string line(PrefixOf(level));
    line += message;
    m_sink.Write(level, line);
}

void Log::WriteFormatted(LogLevel level, std::string_view format, std::format_args arguments)
{
    if (level < m_minimum)
    {
        return;
    }
    std::string text;
    try
    {
        text = std::vformat(format, arguments);
    }
    catch (const std::format_error&)
    {
        // Un aviso con un formato roto no puede cortar el cliente: se escribe el formato, que es lo
        // que hay que arreglar.
        Write(LogLevel::Error, std::string("formato de log inválido: ").append(format));
        return;
    }
    std::string line(PrefixOf(level));
    line += text;
    m_sink.Write(level, line);
}
} // namespace Client::Core
