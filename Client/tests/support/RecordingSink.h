#pragma once

#include "Core/ILogSink.h"

#include <string>
#include <string_view>
#include <vector>

// A log output that stores each line with its level, so that a test reads what was written. Used from a single
// thread: it needs no mutex.
namespace Client::Tests
{
class RecordingSink final : public Core::ILogSink
{
public:
    void Write(Core::LogLevel level, std::string_view line) override
    {
        levels.push_back(level);
        lines.emplace_back(line);
    }

    std::vector<Core::LogLevel> levels;
    std::vector<std::string> lines;
};
} // namespace Client::Tests
