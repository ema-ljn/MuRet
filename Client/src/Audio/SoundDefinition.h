#pragma once

#include <cstdint>
#include <string_view>

namespace Client::Audio
{
struct SoundDefinition
{
    std::int32_t id;
    std::string_view path;
    std::uint8_t voices;
    bool positional;
};
} // namespace Client::Audio
