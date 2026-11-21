#include "App/CommandLine.h"

#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/PathText.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ParseCommandLine: the options both systems share. What each system adds is in
// test_command_line_posix.cpp and test_command_line_windows.cpp.
namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::PacingMode;
using Client::App::ParseCommandLine;
using Client::App::ServerAddress;
using Client::App::UsageErrorExitCode;
using Client::App::VSyncMode;

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

// The line that names what is wrong; the usage follows it.
std::string ErrorOf(std::vector<std::string_view> arguments)
{
    const CommandLineExit ending = ExitOf(arguments);
    EXPECT_EQ(ending.exitCode, UsageErrorExitCode) << ending.text;
    return ending.text.substr(0, ending.text.find('\n'));
}

bool Contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

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
    EXPECT_TRUE(Contains(ErrorOf({"--frames"}), "--frames"));
}

TEST(CommandLineTest, FramesRejectsWhatIsNotAPositiveNumber)
{
    for (const std::string_view value : {"0", "-1", "+5", "12x", "", " 7", "18446744073709551616"})
    {
        EXPECT_EQ(ErrorOf({"--frames", value}),
                  "Main: --frames needs a number greater than 0, not \"" + std::string(value) + "\"");
    }
}

TEST(CommandLineTest, ParsesPacing)
{
    EXPECT_EQ(Parsed({"--pacing", "decoupled"}).pacing, PacingMode::Decoupled);
    EXPECT_EQ(Parsed({"--pacing=faithful"}).pacing, PacingMode::Faithful);
    for (const std::string_view value : {"Faithful", "fiel", ""})
    {
        EXPECT_EQ(ErrorOf({"--pacing", value}),
                  "Main: --pacing needs decoupled or faithful, not \"" + std::string(value) + "\"");
    }
}

TEST(CommandLineTest, ParsesVSync)
{
    EXPECT_EQ(Parsed({"--vsync", "on"}).vsync, VSyncMode::On);
    EXPECT_EQ(Parsed({"--vsync", "adaptive"}).vsync, VSyncMode::Adaptive);
    EXPECT_EQ(Parsed({"--vsync", "off"}).vsync, VSyncMode::Off);
    EXPECT_EQ(ErrorOf({"--vsync", "1"}), "Main: --vsync needs on, adaptive or off, not \"1\"");
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
        EXPECT_EQ(ErrorOf({"--max-fps", text}),
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
    EXPECT_TRUE(Contains(ErrorOf({"--stats=1"}), "stats"));
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
    EXPECT_EQ(ErrorOf({"--host", "10.0.0.1"}), "Main: --host needs --port");
    EXPECT_EQ(ErrorOf({"--port", "44405"}), "Main: --port needs --host");
}

TEST(CommandLineTest, RejectsABadPort)
{
    EXPECT_EQ(ErrorOf({"--host", "10.0.0.1", "--port", "0"}), "Main: --port: the port must be from 1 to 65535");
    EXPECT_EQ(ErrorOf({"--host", "10.0.0.1", "--port", "65536"}), "Main: --port: the port must be from 1 to 65535");
    EXPECT_EQ(ErrorOf({"--host", "10.0.0.1", "--port", "12a"}), "Main: --port: the port must be a number");
    EXPECT_EQ(Parsed({"--host", "10.0.0.1", "--port", "65535"}).connect->port, 65535);
}

// Q-C37: the original copied the host without looking at its length.
TEST(CommandLineTest, AcceptsAHostOf255BytesAndRejects256)
{
    const std::string longest(255, 'a');
    const std::string tooLong(256, 'a');

    EXPECT_EQ(Parsed({"--host", longest, "--port", "44405"}).connect->host.size(), 255u);
    EXPECT_EQ(ErrorOf({"--host", tooLong, "--port", "44405"}), "Main: --host: the host must be 1 to 255 bytes");
}

TEST(CommandLineTest, RejectsAHostThatIsNotAscii)
{
    EXPECT_EQ(ErrorOf({"--host", "m\xFA.org", "--port", "44405"}),
              "Main: --host: the host must be ASCII (a name with accents goes in punycode)");
}

TEST(CommandLineTest, RejectsARepeatedOption)
{
    EXPECT_EQ(ErrorOf({"--frames", "5", "--frames", "7"}), "Main: --frames is repeated");
    EXPECT_EQ(ErrorOf({"--config", "a.ini", "--config=b.ini"}), "Main: --config is repeated");
    EXPECT_EQ(ErrorOf({"--data", "a", "--data", "b"}), "Main: --data is repeated");
    EXPECT_EQ(ErrorOf({"--pacing", "faithful", "--pacing", "decoupled"}), "Main: --pacing is repeated");
    EXPECT_EQ(ErrorOf({"--vsync", "off", "--vsync", "on"}), "Main: --vsync is repeated");
    EXPECT_EQ(ErrorOf({"--max-fps", "60", "--max-fps", "30"}), "Main: --max-fps is repeated");
    EXPECT_EQ(ErrorOf({"--host", "a", "--port", "1", "--host", "b"}), "Main: --host is repeated");
    EXPECT_EQ(ErrorOf({"--stats", "--stats"}), "Main: --stats is repeated");
    EXPECT_EQ(ErrorOf({"--auto-login", "--auto-login"}), "Main: --auto-login is repeated");
}

TEST(CommandLineTest, ParsesTheConfigAndDataPaths)
{
    const CommandLine commandLine = Parsed({"--config", "other.ini", "--data=C:/MU/Data"});

    EXPECT_EQ(commandLine.configPath, std::filesystem::path("other.ini"));
    EXPECT_EQ(commandLine.dataPath, std::filesystem::path("C:/MU/Data"));
    EXPECT_TRUE(Contains(ErrorOf({"--config"}), "--config"));
    EXPECT_TRUE(Contains(ErrorOf({"--data"}), "--data"));
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
        const CommandLineExit ending = ExitOf({help});

        EXPECT_EQ(ending.exitCode, 0);
        EXPECT_TRUE(ending.text.starts_with("Usage: Main [OPTIONS]")) << ending.text;
        for (const std::string_view option :
             {"--help", "--version", "--host HOST", "--port PORT", "--auto-login", "--config PATH", "--data PATH",
              "--frames N", "--pacing MODE", "--vsync MODE", "--max-fps N", "--stats"})
        {
            EXPECT_TRUE(Contains(ending.text, option)) << option << '\n' << ending.text;
        }
    }
}

TEST(CommandLineTest, HelpComesBeforeTheValues)
{
    EXPECT_EQ(ExitOf({"--frames", "0", "--help"}).exitCode, 0);
}

TEST(CommandLineTest, VersionPrintsTheVersion)
{
    const CommandLineExit ending = ExitOf({"--version"});

    EXPECT_EQ(ending.exitCode, 0);
    EXPECT_EQ(ending.text, "MuRet client 1.2.3\n");
}

TEST(CommandLineTest, AUsageErrorShowsTheUsageAfterItsLine)
{
    const CommandLineExit ending = ExitOf({"--frames", "0"});

    EXPECT_EQ(ending.exitCode, UsageErrorExitCode);
    EXPECT_TRUE(Contains(ending.text, "\nUsage: Main [OPTIONS]")) << ending.text;
    EXPECT_TRUE(Contains(ending.text, "--max-fps N")) << ending.text;
}

TEST(CommandLineTest, HostAndPortGoInAnyOrder)
{
    const ServerAddress address = *Parsed({"--port", "1", "--frames", "60", "--host", "mu.example.org"}).connect;

    EXPECT_EQ(address.host, "mu.example.org");
    EXPECT_EQ(address.port, 1);
}
} // namespace
