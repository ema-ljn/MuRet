#include "App/CommandLine.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ParseCommandLine on Linux and the other POSIX systems: GNU options only (Emanuel, 2026-11-21). What is
// not an option is not for MuRet and is ignored; an unknown option is a usage error.
namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::ParseCommandLine;
using Client::App::UsageErrorExitCode;

constexpr std::string_view Version = "1.2.3";

CommandLine Parsed(std::vector<std::string_view> arguments)
{
    auto parsed = ParseCommandLine(arguments, Version);
    if (!parsed.HasValue())
    {
        ADD_FAILURE() << parsed.Error().text;
        return {};
    }
    return std::move(parsed).Value();
}

CommandLineExit ExitOf(std::vector<std::string_view> arguments)
{
    auto parsed = ParseCommandLine(arguments, Version);
    if (parsed.HasValue())
    {
        ADD_FAILURE() << "the command line was accepted";
        return {};
    }
    return parsed.Error();
}

TEST(CommandLinePosixTest, IgnoresTheLauncherForm)
{
    const CommandLine commandLine = Parsed({"connect", "/u10.0.0.1", "/p44405", "--frames", "60"});

    EXPECT_FALSE(commandLine.connect.has_value());
    EXPECT_EQ(commandLine.maxFrames, 60u);
}

TEST(CommandLinePosixTest, IgnoresTheWindowsForm)
{
    const CommandLine commandLine = Parsed({"/frames", "60", "/?", "/y1"});

    EXPECT_FALSE(commandLine.maxFrames.has_value());
}

TEST(CommandLinePosixTest, AnUnknownOptionIsAUsageErrorWithTheUsage)
{
    for (const std::string_view option : {"--fullscreen", "--fullscreen=1", "-x", "--Frames"})
    {
        const CommandLineExit ending = ExitOf({option, "60"});

        EXPECT_EQ(ending.exitCode, UsageErrorExitCode);
        EXPECT_EQ(ending.text.substr(0, ending.text.find('\n')),
                  "Main: unknown option \"" + std::string(option) + "\"");
        EXPECT_NE(ending.text.find("\nUsage: Main [OPTIONS]"), std::string::npos) << ending.text;
    }
}

TEST(CommandLinePosixTest, AfterTheDoubleDashAnUnknownOptionIsIgnored)
{
    EXPECT_FALSE(Parsed({"--", "--fullscreen"}).maxFrames.has_value());
}

TEST(CommandLinePosixTest, APathCanStartWithASlash)
{
    EXPECT_EQ(Parsed({"--data", "/usr/share/mu"}).dataPath, std::filesystem::path("/usr/share/mu"));
}

// The bytes of the file system, as a name saved in Latin-1: they go through as they are.
TEST(CommandLinePosixTest, APathThatIsNotUtf8IsItsBytes)
{
    const CommandLine commandLine = Parsed({"--data", "/mu/\xDA", "--config", "\xC0\xAF.ini"});

    EXPECT_EQ(commandLine.dataPath, std::filesystem::path("/mu/\xDA"));
    EXPECT_EQ(commandLine.configPath, std::filesystem::path("\xC0\xAF.ini"));
}

TEST(CommandLinePosixTest, TheUsageHasNoWindowsForm)
{
    const CommandLineExit ending = ExitOf({"--help"});

    EXPECT_EQ(ending.text.find("connect /u"), std::string::npos) << ending.text;
}
} // namespace
