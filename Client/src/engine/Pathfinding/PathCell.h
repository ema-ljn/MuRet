#pragma once

#include <cstdint>

namespace Client::GameLogic
{
struct PathCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;

    friend bool operator==(const PathCell&, const PathCell&) = default;
};
} // namespace Client::GameLogic
