#pragma once

#include "App/CommandLine.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// App::ParseCommandLine with a fixed version, for the shared tests and those of each system.
namespace Client::Tests
{
inline constexpr std::string_view CommandLineVersion = "1.2.3";

inline App::CommandLine Parsed(std::vector<std::string_view> arguments)
{
    auto parsed = App::ParseCommandLine(arguments, CommandLineVersion);
    if (!parsed.HasValue())
    {
        ADD_FAILURE() << parsed.Error().text;
        return {};
    }
    return std::move(parsed).Value();
}

inline App::CommandLineExit ExitOf(std::vector<std::string_view> arguments)
{
    auto parsed = App::ParseCommandLine(arguments, CommandLineVersion);
    if (parsed.HasValue())
    {
        ADD_FAILURE() << "the command line was accepted";
        return {};
    }
    return parsed.Error();
}

// The line that names what is wrong; the usage follows it.
inline std::string UsageErrorLine(std::vector<std::string_view> arguments)
{
    const App::CommandLineExit earlyExit = ExitOf(std::move(arguments));
    EXPECT_EQ(earlyExit.exitCode, App::UsageErrorExitCode) << earlyExit.text;
    return earlyExit.text.substr(0, earlyExit.text.find('\n'));
}

inline bool Contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}
} // namespace Client::Tests
