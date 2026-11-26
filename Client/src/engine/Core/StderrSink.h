#pragma once

#include "Core/ILogSink.h"
#include "EngineApi.h"

#include <cstdio>
#include <mutex>
#include <string_view>

// One fwrite per line: the FILE lock (POSIX and the MSVC CRT) keeps it whole against other stderr writers that bypass
// this (an fprintf, SDL's default output). The std::mutex orders this sink's lines. stderr is unbuffered.
namespace Client::Core
{
class ENGINE_API StderrSink final : public ILogSink
{
public:
    // Not owned: it is not closed. A test passes a std::tmpfile() to read what was written.
    explicit StderrSink(std::FILE* output = stderr);

    void Write(LogLevel level, std::string_view line) override;

private:
    std::FILE* m_output;
    std::mutex m_mutex;
};
} // namespace Client::Core
