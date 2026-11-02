#include "Core/FileSink.h"

#include "Core/PathText.h"

#include <system_error>
#include <utility>

namespace Client::Core
{
Expected<std::unique_ptr<FileSink>, std::string> FileSink::Open(const std::filesystem::path& path)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    const bool overTheCap = !error && size > MaxLogFileBytes;

    std::ofstream file(path, overTheCap ? (std::ios::out | std::ios::trunc) : (std::ios::out | std::ios::app));
    if (!file.is_open())
    {
        return Unexpected("no se pudo abrir el archivo de log " + PathText(path));
    }
    // Con new y no make_unique: el constructor es privado.
    return std::unique_ptr<FileSink>(new FileSink(std::move(file)));
}

FileSink::FileSink(std::ofstream file) : m_file(std::move(file)) {}

void FileSink::Write(LogLevel /*level*/, std::string_view line)
{
    std::string text(line);
    text += '\n';
    const std::scoped_lock lock(m_mutex);
    m_file.write(text.data(), static_cast<std::streamsize>(text.size()));
    m_file.flush();
}
} // namespace Client::Core
