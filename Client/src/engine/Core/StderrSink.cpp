#include "Core/StderrSink.h"

#include "Core/ILogSink.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace Client::Core
{
StderrSink::StderrSink(std::FILE* output) : m_output(output) {}

void StderrSink::Write(LogLevel /*level*/, std::string_view line)
{
    std::string text(line);
    text += '\n';
    const std::scoped_lock lock(m_mutex);
    std::fwrite(text.data(), 1, text.size(), m_output);
}
} // namespace Client::Core
