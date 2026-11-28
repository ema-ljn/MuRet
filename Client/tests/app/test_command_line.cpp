#include "App/CommandLine.h"

#include "Kernel/PacingMode.h"
#include "Session/ServerAddress.h"
#include "Platform/VSyncMode.h"
#include "Core/PathText.h"
#include "support/CommandLineTesting.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// Client::App::ParseCommandLine: the options both systems share. What each system adds is in
// test_command_line_posix.cpp and test_command_line_windows.cpp.
namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::UsageErrorExitCode;
using Client::Kernel::PacingMode;
using Client::Platform::VSyncMode;
using Client::Session::ServerAddress;
using Client::Tests::Contains;
using Client::Tests::ExitOf;
using Client::Tests::Parsed;
using Client::Tests::UsageErrorLine;

TEST(CommandLineTest, NoArgumentsSetNothing)
{
    const CommandLine commandLine = Parsed({});

    EXPECT_FALSE(commandLine.maxFrames.has_value());
    EXPECT_FALSE(commandLine.pacing.has_value());
    EXPECT_FALSE(commandLine.vsync.has_value());
    EXPECT_FALSE(commandLine.maxFps.has_value());
    EXPECT_FALSE(commandLine.stats);
    EXPECT_FALSE(commandLine.autoLogin);
    EXPECT_FALSE(commandLine.connect.has_value());
    EXPECT_FALSE(commandLine.configPath.has_value());
    EXPECT_FALSE(commandLine.dataPath.has_value());
}

TEST(CommandLineTest, AnOptionTakesItsValueAfterASpaceOrAnEqualsSign)
{
    EXPECT_EQ(Parsed({"--frames", "60"}).maxFrames, 60u);
    EXPECT_EQ(Parsed({"--frames=60"}).maxFrames, 60u);
}

TEST(CommandLineTest, FramesWithoutANumberIsAUsageError)
{
    EXPECT_TRUE(Contains(UsageErrorLine({"--frames"}), "--frames"));
}

TEST(CommandLineTest, FramesRejectsWhatIsNotAPositiveNumber)
{
    for (const std::string_view value : {"0", "-1", "+5", "12x", "", " 7", "18446744073709551616"})
    {
        EXPECT_EQ(UsageErrorLine({"--frames", value}),
                  "Main: --frames needs a number greater than 0, not \"" + std::string(value) + "\"");
    }
}

TEST(CommandLineTest, ParsesPacing)
{
    EXPECT_EQ(Parsed({"--pacing", "decoupled"}).pacing, PacingMode::Decoupled);
    EXPECT_EQ(Parsed({"--pacing=faithful"}).pacing, PacingMode::Faithful);
    for (const std::string_view value : {"Faithful", "fiel", ""})
    {
        EXPECT_EQ(UsageErrorLine({"--pacing", value}),
                  "Main: --pacing needs decoupled or faithful, not \"" + std::string(value) + "\"");
    }
}

TEST(CommandLineTest, ParsesVSync)
{
    EXPECT_EQ(Parsed({"--vsync", "on"}).vsync, VSyncMode::On);
    EXPECT_EQ(Parsed({"--vsync", "adaptive"}).vsync, VSyncMode::Adaptive);
    EXPECT_EQ(Parsed({"--vsync", "off"}).vsync, VSyncMode::Off);
    EXPECT_EQ(UsageErrorLine({"--vsync", "1"}), "Main: --vsync needs on, adaptive or off, not \"1\"");
}

TEST(CommandLineTest, ParsesMaxFps)
{
    for (const auto& [text, maxFps] :
         {std::pair{"144", 144}, std::pair{"0", 0}, std::pair{"25", 25}, std::pair{"1000", 1000}})
    {
        EXPECT_EQ(Parsed({"--max-fps", text}).maxFps, maxFps);
    }
}

TEST(CommandLineTest, RejectsABadMaxFps)
{
    // Below MinFpsLimit some frames would run two steps; 4294967296 does not fit in an unsigned.
    for (const std::string_view text : {"1001", "1", "24", "-1", "-0", "+5", "60fps", "", "4294967296", "99999999999"})
    {
        EXPECT_EQ(UsageErrorLine({"--max-fps", text}),
                  "Main: --max-fps needs 0 or a number from 25 to 1000, not \"" + std::string(text) + "\"");
    }
}

TEST(CommandLineTest, ParsesTheFlags)
{
    const CommandLine commandLine = Parsed({"--stats", "--auto-login", "--host", "10.0.0.1", "--port", "44405"});

    EXPECT_TRUE(commandLine.stats);
    EXPECT_TRUE(commandLine.autoLogin);
}

TEST(CommandLineTest, AFlagTakesNoValue)
{
    EXPECT_EQ(UsageErrorLine({"--stats=1"}), "Main: --stats takes no value");
    EXPECT_EQ(UsageErrorLine({"--stats="}), "Main: --stats takes no value");
    EXPECT_EQ(UsageErrorLine({"--auto-login=0"}), "Main: --auto-login takes no value");
}

// CLI11 alone would give --config the next argument, as if the = were a space.
TEST(CommandLineTest, AnEmptyValueAfterTheEqualsSignIsAUsageError)
{
    EXPECT_EQ(UsageErrorLine({"--config=", "--frames", "5"}), "Main: --config needs a value");
    EXPECT_EQ(UsageErrorLine({"--frames="}), "Main: --frames needs a value");
}

// The faithful pacing without VSync is checked by Resolve (ConfigTest.FaithfulWithVSyncOnIsAnError).
TEST(CommandLineTest, LeavesTheFaithfulRuleToResolve)
{
    EXPECT_EQ(Parsed({"--pacing", "faithful", "--vsync", "on"}).vsync, VSyncMode::On);
}

TEST(CommandLineTest, ParsesHostAndPort)
{
    const CommandLine commandLine = Parsed({"--host", "10.0.0.1", "--port=55901"});

    ASSERT_TRUE(commandLine.connect.has_value());
    EXPECT_EQ(commandLine.connect->host, "10.0.0.1");
    EXPECT_EQ(commandLine.connect->port, 55901);
}

TEST(CommandLineTest, HostAndPortGoTogether)
{
    EXPECT_EQ(UsageErrorLine({"--host", "10.0.0.1"}), "Main: --host needs --port");
    EXPECT_EQ(UsageErrorLine({"--port", "44405"}), "Main: --port needs --host");
}

TEST(CommandLineTest, RejectsABadPort)
{
    EXPECT_EQ(UsageErrorLine({"--host", "10.0.0.1", "--port", "0"}), "Main: --port: the port must be from 1 to 65535");
    EXPECT_EQ(UsageErrorLine({"--host", "10.0.0.1", "--port", "65536"}),
              "Main: --port: the port must be from 1 to 65535");
    EXPECT_EQ(UsageErrorLine({"--host", "10.0.0.1", "--port", "12a"}), "Main: --port: the port must be a number");
}

TEST(CommandLineTest, ThePortGoesFrom1To65535)
{
    EXPECT_EQ(Parsed({"--host", "10.0.0.1", "--port", "1"}).connect->port, 1);
    EXPECT_EQ(Parsed({"--host", "10.0.0.1", "--port", "65535"}).connect->port, 65535);
}

TEST(CommandLineTest, RejectsAnEmptyHost)
{
    EXPECT_EQ(UsageErrorLine({"--host", "", "--port", "44405"}), "Main: --host: the host must be 1 to 255 bytes");
}

// Q-C37: the original copied the host without looking at its length.
TEST(CommandLineTest, AcceptsAHostOf255BytesAndRejects256)
{
    const std::string longest(255, 'a');
    const std::string tooLong(256, 'a');

    EXPECT_EQ(Parsed({"--host", longest, "--port", "44405"}).connect->host.size(), 255u);
    EXPECT_EQ(UsageErrorLine({"--host", tooLong, "--port", "44405"}), "Main: --host: the host must be 1 to 255 bytes");
}

TEST(CommandLineTest, RejectsAHostThatIsNotAscii)
{
    EXPECT_EQ(UsageErrorLine({"--host", "m\xFA.org", "--port", "44405"}),
              "Main: --host: the host must be ASCII (a name with accents goes in punycode)");
}

TEST(CommandLineTest, RejectsARepeatedOption)
{
    EXPECT_EQ(UsageErrorLine({"--frames", "5", "--frames", "7"}), "Main: --frames is repeated");
    EXPECT_EQ(UsageErrorLine({"--config", "a.ini", "--config=b.ini"}), "Main: --config is repeated");
    EXPECT_EQ(UsageErrorLine({"--data", "a", "--data", "b"}), "Main: --data is repeated");
    EXPECT_EQ(UsageErrorLine({"--pacing", "faithful", "--pacing", "decoupled"}), "Main: --pacing is repeated");
    EXPECT_EQ(UsageErrorLine({"--vsync", "off", "--vsync", "on"}), "Main: --vsync is repeated");
    EXPECT_EQ(UsageErrorLine({"--max-fps", "60", "--max-fps", "30"}), "Main: --max-fps is repeated");
    EXPECT_EQ(UsageErrorLine({"--host", "a", "--port", "1", "--host", "b"}), "Main: --host is repeated");
    EXPECT_EQ(UsageErrorLine({"--stats", "--stats"}), "Main: --stats is repeated");
    EXPECT_EQ(UsageErrorLine({"--auto-login", "--auto-login"}), "Main: --auto-login is repeated");
}

TEST(CommandLineTest, ParsesTheConfigAndDataPaths)
{
    const CommandLine commandLine = Parsed({"--config", "other.ini", "--data=C:/MU/Data"});

    EXPECT_EQ(commandLine.configPath, std::filesystem::path("other.ini"));
    EXPECT_EQ(commandLine.dataPath, std::filesystem::path("C:/MU/Data"));
}

TEST(CommandLineTest, APathOptionWithoutAValueIsAUsageError)
{
    EXPECT_TRUE(Contains(UsageErrorLine({"--config"}), "--config"));
    EXPECT_TRUE(Contains(UsageErrorLine({"--data"}), "--data"));
}

// The arguments come in UTF-8 (main.cpp uses SDL_main.h, which builds them with GetCommandLineW on Windows): the
// path is read as UTF-8, not in the ANSI code page. "Ł" (U+0141) is not in 1252.
TEST(CommandLineTest, ReadsAPathAsUtf8)
{
    const CommandLine commandLine = Parsed({"--data", "C:/Games/\xC5\x81ukasz/Data", "--config", "Espa\xC3\xB1ol.ini"});

    EXPECT_EQ(Client::Core::PathText(*commandLine.dataPath), "C:/Games/\xC5\x81ukasz/Data");
    EXPECT_EQ(Client::Core::PathText(*commandLine.configPath), "Espa\xC3\xB1ol.ini");
}

TEST(CommandLineTest, TheDoubleDashEndsTheOptions)
{
    const CommandLine commandLine = Parsed({"--frames", "60", "--", "--frames", "7", "--fullscreen", "/y1"});

    EXPECT_EQ(commandLine.maxFrames, 60u);
}

TEST(CommandLineTest, HelpPrintsTheUsageWithEveryOption)
{
    for (const std::string_view help : {"--help", "-h"})
    {
        const CommandLineExit earlyExit = ExitOf({help});

        EXPECT_EQ(earlyExit.exitCode, 0);
        EXPECT_TRUE(earlyExit.text.starts_with("Usage: Main [OPTIONS]")) << earlyExit.text;
        for (const std::string_view option :
             {"--help", "--version", "--host HOST", "--port PORT", "--auto-login", "--config PATH", "--data PATH",
              "--frames N", "--pacing MODE", "--vsync MODE", "--max-fps N", "--stats"})
        {
            EXPECT_TRUE(Contains(earlyExit.text, option)) << option << '\n' << earlyExit.text;
        }
    }
}

TEST(CommandLineTest, HelpComesBeforeTheValues)
{
    EXPECT_EQ(ExitOf({"--frames", "0", "--help"}).exitCode, 0);
}

TEST(CommandLineTest, VersionPrintsTheVersion)
{
    const CommandLineExit earlyExit = ExitOf({"--version"});

    EXPECT_EQ(earlyExit.exitCode, 0);
    EXPECT_EQ(earlyExit.text, "MuRet client 1.2.3\n");
}

TEST(CommandLineTest, AUsageErrorShowsTheUsageAfterItsLine)
{
    const CommandLineExit earlyExit = ExitOf({"--frames", "0"});

    EXPECT_EQ(earlyExit.exitCode, UsageErrorExitCode);
    EXPECT_TRUE(Contains(earlyExit.text, "\nUsage: Main [OPTIONS]")) << earlyExit.text;
    EXPECT_TRUE(Contains(earlyExit.text, "--max-fps N")) << earlyExit.text;
}

TEST(CommandLineTest, HostAndPortGoInAnyOrder)
{
    const ServerAddress address = *Parsed({"--port", "1", "--frames", "60", "--host", "mu.example.org"}).connect;

    EXPECT_EQ(address.host, "mu.example.org");
    EXPECT_EQ(address.port, 1);
}
} // namespace
