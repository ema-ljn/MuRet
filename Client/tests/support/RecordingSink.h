#pragma once

#include "Core/ILogSink.h"

#include <string>
#include <string_view>
#include <vector>

// Una salida del log que guarda cada línea con su nivel, para que un test lea lo que se escribió.
// Se usa desde un solo hilo: no necesita mutex.
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
