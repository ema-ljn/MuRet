#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// What each system adds to the options of CommandLine.cpp: CMake builds CommandLinePosix.cpp or CommandLineWindows.cpp.
namespace Client::App
{
enum class OptionStyle : std::uint8_t
{
    Gnu,
    // Also /name value and /name:value, with the names in any case.
    GnuAndWindows,
};

struct SystemSyntax
{
    OptionStyle style = OptionStyle::Gnu;
    // After "Usage: Main [OPTIONS]", or empty.
    std::string_view operands;
    // After the options in the help, or empty.
    std::string_view helpFooter;
    // An argument that prints the help before any error, or empty.
    std::string_view helpWord;
};

// The host and the port as typed, with the names that typed them: --host and --port, or the launcher's /u and /p.
struct ServerTexts
{
    std::string hostName;
    std::optional<std::string> host;
    std::string portName;
    std::optional<std::string> port;
};

SystemSyntax ThisSystemSyntax();

// The arguments before "--" that are not MuRet's options: the launcher's server, if any, or a usage error's line.
Core::Expected<std::optional<ServerTexts>, std::string> ReadSystemArguments(std::span<const std::string> arguments);

// The path of --config or --data, or nullopt if the system needs it in UTF-8 and it is not.
std::optional<std::filesystem::path> PathFromArgument(std::string_view argument);
} // namespace Client::App
