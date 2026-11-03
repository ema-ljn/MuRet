#include "Core/FileSink.h"

#include "Core/PathText.h"

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
        // El vaciado es solo al abrir: después también agrega al final para no pisar lo que
        // escriba otro sink. Mantener trunc dejaría este stream con su propio cursor (Q-C48).
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
    // Con new y no make_unique: el constructor es privado.
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
        return; // falló antes, y ya avisó
    }
    m_file.write(text.data(), static_cast<std::streamsize>(text.size()));
    m_file.flush();
    if (!m_file)
    {
        // Se cierra en el momento. En libstdc++, lo que no se pudo escribir queda en el buffer del
        // ofstream: close lo prueba una vez más, ahora, y abierto lo mandaría su destructor al
        // salir, cuando quizás ya hay lugar. Después no se intenta más. El aviso, una sola vez y
        // con un fwrite, no por el Log: desde un sink, el Log volvería a entrar al Tee, que tiene
        // su mutex tomado.
        m_file.close();
        std::fwrite(m_failureNotice.data(), 1, m_failureNotice.size(), m_noticeOutput);
    }
}
} // namespace Client::Core
