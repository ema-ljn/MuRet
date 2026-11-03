#include "Core/TeeSink.h"

#include "Core/ILogSink.h"

#include <mutex>
#include <string_view>

namespace Client::Core
{
TeeSink::TeeSink(ILogSink& first, ILogSink& second) : m_first(first), m_second(second) {}

void TeeSink::Write(LogLevel level, std::string_view line)
{
    const std::scoped_lock lock(m_mutex);
    m_first.Write(level, line);
    m_second.Write(level, line);
}
} // namespace Client::Core
