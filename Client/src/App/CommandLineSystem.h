#pragma once

#include "App/ServerAddress.h"
#include "Core/Expected.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// What each system adds to the options of CommandLine.cpp. CMake builds one of CommandLinePosix.cpp and
// CommandLineWindows.cpp.
namespace Client::App
{
enum class OptionStyle
{
    Gnu,
    // Also /name value and /name:value, with the names in any case.
    GnuAndWindows,
};

struct Leftovers
{
    // connect /u<host> /p<port>.
    std::optional<ServerAddress> launcher;
    // /?.
    bool help = false;
};

OptionStyle SystemOptionStyle();

std::string_view SystemUsage();

// The text after the options in the help, or empty.
std::string_view SystemHelpFooter();

// The arguments that are not MuRet's options, in order, with "--" among them if it was given. The error is a usage
// error's line.
Core::Expected<Leftovers, std::string> ReadLeftovers(std::span<const std::string> arguments);
} // namespace Client::App
