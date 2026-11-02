#include "Core/Log.h"

#include <cstdio>
#include <utility>

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
    return "[error] ";
}

// La ruta como texto UTF-8 para el mensaje. path::string() en Windows la pasa a la página de
// códigos del sistema, y una letra que no está ahí tira una excepción.
std::string PathText(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}
} // namespace

TeeSink::TeeSink(ILogSink& first, ILogSink& second) : m_first(first), m_second(second) {}

void TeeSink::Write(LogLevel level, std::string_view line)
{
    const std::scoped_lock lock(m_mutex);
    m_first.Write(level, line);
    m_second.Write(level, line);
}

void StderrSink::Write(LogLevel /*level*/, std::string_view line)
{
    const std::scoped_lock lock(m_mutex);
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
}

Expected<std::unique_ptr<FileSink>, std::string> FileSink::Open(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        return Unexpected("no se pudo abrir el archivo de log " + PathText(path));
    }
    return std::make_unique<FileSink>(std::move(file));
}

FileSink::FileSink(std::ofstream file) : m_file(std::move(file)) {}

void FileSink::Write(LogLevel /*level*/, std::string_view line)
{
    const std::scoped_lock lock(m_mutex);
    m_file << line << '\n';
    m_file.flush();
}

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
    std::string line(PrefixOf(level));
    line += std::vformat(format, arguments);
    m_sink.Write(level, line);
}
} // namespace Client::Core
