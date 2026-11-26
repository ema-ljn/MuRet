#pragma once

#include <cstdio>
#include <memory>
#include <string>

// A C temporary file, for the tests that pass a std::FILE* to an output instead of stderr (StderrSink, FileSink's
// warning), and reading what was written.
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

// Deleted when closed. nullptr if it could not be created. On MSVC, std::tmpfile gives warning C4996 (an error with
// /WX) and asks for tmpfile_s.
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

// Everything in the file, from the start.
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
