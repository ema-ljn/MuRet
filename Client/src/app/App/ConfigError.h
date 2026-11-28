#pragma once

#include <cstddef>
#include <string>

namespace Client::App
{
struct ConfigError
{
    // The first error's line, counted from 1.
    std::size_t line = 0;
    // For the user, without the line: whoever prints it puts the line first.
    std::string message;
};
} // namespace Client::App
