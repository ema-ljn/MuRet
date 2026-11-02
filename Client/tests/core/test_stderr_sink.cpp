#include "Core/StderrSink.h"

#include "Core/Log.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

// Client::Core::StderrSink, contra un archivo temporal en vez de stderr.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::StderrSink;

struct FileCloser
{
    void operator()(std::FILE* file) const
    {
        std::fclose(file);
    }
};

// Un archivo temporal que se borra al cerrarlo. En MSVC, std::tmpfile da el aviso C4996 (que es un
// error con /WX) y pide tmpfile_s.
std::unique_ptr<std::FILE, FileCloser> OpenTemporaryFile()
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    if (tmpfile_s(&file) != 0)
    {
        return nullptr;
    }
    return std::unique_ptr<std::FILE, FileCloser>(file);
#else
    return std::unique_ptr<std::FILE, FileCloser>(std::tmpfile());
#endif
}

TEST(StderrSinkTest, WritesOneLinePerMessage)
{
    const auto output = OpenTemporaryFile();
    ASSERT_NE(output, nullptr);
    StderrSink sink(output.get());
    Log log(sink, LogLevel::Info);

    log.Info("uno");
    log.Error("dos {}", 2);

    std::rewind(output.get());
    std::string text;
    for (int c = std::fgetc(output.get()); c != EOF; c = std::fgetc(output.get()))
    {
        text += static_cast<char>(c);
    }
    EXPECT_EQ(text, "[info] uno\n[error] dos 2\n");
}
} // namespace
