#pragma once

#include "Audio/IMusicStream.h"

#include <string>
#include <string_view>
#include <vector>

namespace Client::Tests
{
class RecordingMusicStream final : public Audio::IMusicStream
{
public:
    void Play(std::string_view path) override
    {
        played.emplace_back(path);
        events.emplace_back("play:" + std::string(path));
    }
    void Stop() override
    {
        ++stops;
        events.emplace_back("stop");
    }
    [[nodiscard]] int ProgressPercent() const override
    {
        return progress;
    }

    std::vector<std::string> played;
    std::vector<std::string> events;
    int stops = 0;
    int progress = 0;
};
} // namespace Client::Tests
