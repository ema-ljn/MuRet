#include "Core/FileSink.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/PathText.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Client::Core
{
Expected<std::unique_ptr<FileSink>, std::string> FileSink::Open(const std::filesystem::path& path,
                                                                std::FILE* noticeOutput)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    const bool overTheCap = !error && size > MaxLogFileBytes;

    std::ofstream file(path, overTheCap ? (std::ios::out | std::ios::trunc) : (std::ios::out | std::ios::app));
    if (overTheCap && file.is_open())
    {
        // Truncated only on open: afterwards it also appends, so it does not overwrite what another sink writes.
        // Keeping trunc would leave this stream with its own cursor (Q-C48).
        file.close();
        if (file)
        {
            file.open(path, std::ios::out | std::ios::app);
        }
    }
    if (!file.is_open())
    {
        return Unexpected("no se pudo abrir el archivo de log " + PathText(path));
    }
    // new and not make_unique: the constructor is private.
    std::string notice =
        "[warning] no se pudo escribir en el archivo de log " + PathText(path) + ": el log sigue solo en stderr\n";
    return std::unique_ptr<FileSink>(new FileSink(std::move(file), std::move(notice), noticeOutput));
}

FileSink::FileSink(std::ofstream file, std::string failureNotice, std::FILE* noticeOutput)
    : m_file(std::move(file)), m_failureNotice(std::move(failureNotice)), m_noticeOutput(noticeOutput)
{
}

void FileSink::Write(LogLevel /*level*/, std::string_view line)
{
    std::string text(line);
    text += '\n';
    const std::scoped_lock lock(m_mutex);
    if (!m_file.is_open())
    {
        return; // failed before and already warned
    }
    m_file.write(text.data(), static_cast<std::streamsize>(text.size()));
    m_file.flush();
    if (!m_file)
    {
        // Closed now: in libstdc++ the unwritten bytes stay in the ofstream's buffer and close retries them once; left
        // open, its destructor would send them at exit, when there may be room. The notice goes once with fwrite, not
        // through the Log: from a sink, the Log would re-enter the Tee, which holds its mutex.
        m_file.close();
        std::fwrite(m_failureNotice.data(), 1, m_failureNotice.size(), m_noticeOutput);
    }
}
} // namespace Client::Core
