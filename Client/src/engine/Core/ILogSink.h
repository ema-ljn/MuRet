#pragma once

#include <cstdint>
#include <string_view>

namespace Client::Core
{
enum class LogLevel : std::uint8_t
{
    Debug,
    Info,
    Warning,
    Error
};

class ILogSink
{
public:
    virtual ~ILogSink() = default;

    // `line` has the level prefix ("[info] ") and no line end: the sink adds it. It can arrive from several threads at
    // once (SDL logs from its own), so a sink that writes to something shared guards it with its std::mutex. That mutex
    // is not recursive, so a Write cannot log or call SDL (which may log): it would re-enter its own mutex or the
    // TeeSink's and hang.
    virtual void Write(LogLevel level, std::string_view line) = 0;

protected:
    // Like IObfuscation: copied and moved only from a derived class, so assigning through two ILogSink& does not copy
    // only the base part (slicing).
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
    ILogSink(ILogSink&&) = default;
    ILogSink& operator=(ILogSink&&) = default;
};
} // namespace Client::Core
