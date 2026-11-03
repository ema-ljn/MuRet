#pragma once

#include <cstdio>
#include <memory>
#include <string>

// Un archivo temporal de C, para los tests que le pasan un std::FILE* a una salida en vez de
// stderr (StderrSink, el aviso de FileSink), y la lectura de lo que quedó escrito.
namespace Client::Tests
{
struct FileCloser
{
    void operator()(std::FILE* file) const
    {
        std::fclose(file);
    }
};

using TemporaryFile = std::unique_ptr<std::FILE, FileCloser>;

// Se borra al cerrarlo. Si no se pudo crear, nullptr. En MSVC, std::tmpfile da el aviso C4996 (que
// es un error con /WX) y pide tmpfile_s.
inline TemporaryFile OpenTemporaryFile()
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    if (tmpfile_s(&file) != 0)
    {
        return nullptr;
    }
    return TemporaryFile(file);
#else
    return TemporaryFile(std::tmpfile());
#endif
}

// Todo lo que tiene el archivo, desde el principio.
inline std::string ReadAll(std::FILE* file)
{
    std::rewind(file);
    std::string text;
    for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file))
    {
        text += static_cast<char>(c);
    }
    return text;
}
} // namespace Client::Tests
