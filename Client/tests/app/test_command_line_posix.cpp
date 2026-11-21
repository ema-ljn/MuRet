#include "App/CommandLine.h"

#include "support/CommandLineTesting.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

// Client::App::ParseCommandLine on Linux and the other POSIX systems: GNU options only. What is not an option is not
// for MuRet and is ignored; an unknown option is a usage error.
namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::Tests::Contains;
using Client::Tests::ExitOf;
using Client::Tests::Parsed;
using Client::Tests::UsageErrorLine;

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
    for (const std::string_view option : {"--fullscreen", "--fullscreen=1", "--fullscreen=", "-x", "--Frames"})
    {
        EXPECT_EQ(UsageErrorLine({option, "60"}), "Main: unknown option \"" + std::string(option) + "\"");
        const CommandLineExit earlyExit = ExitOf({option, "60"});
        EXPECT_TRUE(Contains(earlyExit.text, "\nUsage: Main [OPTIONS]")) << earlyExit.text;
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
    const CommandLineExit earlyExit = ExitOf({"--help"});

    EXPECT_FALSE(Contains(earlyExit.text, "connect /u")) << earlyExit.text;
}
} // namespace
