#include "Core/Log.h"

#include "Core/ILogSink.h"

#include <format>
#include <string>
#include <string_view>

namespace Client::Core
{
std::string_view LevelPrefix(LogLevel level)
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
    // A value that is none of the four (a static_cast of a number) is written as error, the most visible.
    return "[error] ";
}

Log::Log(ILogSink& sink, LogLevel minimum) : m_sink(sink), m_minimum(minimum) {}

void Log::Write(LogLevel level, std::string_view message)
{
    if (level < m_minimum)
    {
        return;
    }
    std::string line(LevelPrefix(level));
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
        // A message with a broken format cannot stop the client: the format is written, since that is what needs
        // fixing.
        Write(LogLevel::Error, std::string("formato de log inválido: ").append(format));
        return;
    }
    std::string line(LevelPrefix(level));
    line += text;
    m_sink.Write(level, line);
}
} // namespace Client::Core
