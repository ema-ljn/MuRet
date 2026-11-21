#include "App/CommandLine.h"

#include "App/ServerAddress.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::App::ParseCommandLine on Windows: the options also take /name value and /name:value in any case, and the
// launcher's connect /u<host> /p<port> sets the server, as today (Q-C36, Q-C37).
namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::ParseCommandLine;
using Client::App::ServerAddress;
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

std::string ErrorOf(std::vector<std::string_view> arguments)
{
    const CommandLineExit ending = ExitOf(arguments);
    EXPECT_EQ(ending.exitCode, UsageErrorExitCode) << ending.text;
    return ending.text.substr(0, ending.text.find('\n'));
}

ServerAddress ConnectOf(std::vector<std::string_view> arguments)
{
    const CommandLine commandLine = Parsed(std::move(arguments));
    if (!commandLine.connect)
    {
        ADD_FAILURE() << "no server";
        return {};
    }
    return *commandLine.connect;
}

TEST(CommandLineWindowsTest, TakesTheWindowsForm)
{
    EXPECT_EQ(Parsed({"/frames", "60"}).maxFrames, 60u);
    EXPECT_EQ(Parsed({"/frames:60"}).maxFrames, 60u);
    EXPECT_EQ(Parsed({"/data:C:\\MU\\Data"}).dataPath, std::filesystem::path("C:\\MU\\Data"));
    EXPECT_TRUE(Parsed({"/stats"}).stats);
}

TEST(CommandLineWindowsTest, TakesTheNamesInAnyCase)
{
    EXPECT_EQ(Parsed({"/FRAMES:60"}).maxFrames, 60u);
    EXPECT_EQ(Parsed({"--Frames", "60"}).maxFrames, 60u);
    EXPECT_EQ(ConnectOf({"/HOST", "10.0.0.1", "/Port:44405"}).port, 44405);
}

TEST(CommandLineWindowsTest, PrintsTheHelpWithTheLauncherForm)
{
    for (const std::string_view help : {"/?", "/help", "/HELP", "/h", "--help"})
    {
        const CommandLineExit ending = ExitOf({help});

        EXPECT_EQ(ending.exitCode, 0) << help;
        EXPECT_TRUE(ending.text.starts_with("Usage: Main [OPTIONS] [connect /u<host> /p<port>]")) << ending.text;
        EXPECT_NE(ending.text.find("/name:value"), std::string::npos) << ending.text;
    }
}

TEST(CommandLineWindowsTest, TheQuestionMarkComesBeforeAnError)
{
    EXPECT_EQ(ExitOf({"/y1", "/?"}).exitCode, 0);
}

TEST(CommandLineWindowsTest, AcceptsConnectWithHostAndPort)
{
    const ServerAddress address = ConnectOf({"connect", "/u10.0.0.1", "/p44405"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 44405);
}

TEST(CommandLineWindowsTest, IgnoresTheCase)
{
    const ServerAddress address = ConnectOf({"CONNECT", "/U10.0.0.1", "/P44405"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 44405);
}

TEST(CommandLineWindowsTest, AcceptsAnyOrder)
{
    const ServerAddress address = ConnectOf({"/p55901", "--frames", "60", "/u10.0.0.1"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 55901);
}

TEST(CommandLineWindowsTest, ConnectIsOptional)
{
    EXPECT_EQ(ConnectOf({"/u10.0.0.1", "/p44405"}).host, "10.0.0.1");
}

TEST(CommandLineWindowsTest, ConnectAloneIsIgnored)
{
    EXPECT_FALSE(Parsed({"connect"}).connect.has_value());
}

TEST(CommandLineWindowsTest, AcceptsTheAutoLoginWithTheLauncherForm)
{
    const CommandLine commandLine = Parsed({"--auto-login", "connect", "/u10.0.0.1", "/p44405"});

    EXPECT_TRUE(commandLine.autoLogin);
    EXPECT_TRUE(commandLine.connect.has_value());
}

TEST(CommandLineWindowsTest, RejectsAPortOutOfRange)
{
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1", "/p0"}), "Main: /p: the port must be from 1 to 65535");
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1", "/p65536"}), "Main: /p: the port must be from 1 to 65535");
    EXPECT_EQ(ConnectOf({"/u10.0.0.1", "/p65535"}).port, 65535);
    EXPECT_EQ(ConnectOf({"/u10.0.0.1", "/p1"}).port, 1);
}

TEST(CommandLineWindowsTest, RejectsAPortWithLetters)
{
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p12a"}), "Main: /p: the port must be a number");
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p"}), "Main: /p needs the port attached (/p44405)");
}

TEST(CommandLineWindowsTest, RejectsHostWithoutPort)
{
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1"}), "Main: /u needs /p with the port");
}

TEST(CommandLineWindowsTest, RejectsPortWithoutHost)
{
    EXPECT_EQ(ErrorOf({"/p44405"}), "Main: /p needs /u with the host");
}

TEST(CommandLineWindowsTest, RejectsAnEmptyHost)
{
    EXPECT_EQ(ErrorOf({"/u", "/p44405"}), "Main: /u needs the host attached (/u10.0.0.1)");
    EXPECT_EQ(ErrorOf({"/u", "10.0.0.1", "/p44405"}), "Main: /u needs the host attached (/u10.0.0.1)");
}

TEST(CommandLineWindowsTest, RejectsARepeatedHostOrPort)
{
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/U10.0.0.2", "/p1"}), "Main: /U is repeated");
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p1", "/p2"}), "Main: /p is repeated");
}

// Q-C37, on the command line: the same ParseHost as the config.
TEST(CommandLineWindowsTest, RejectsAHostThatIsNotAscii)
{
    EXPECT_EQ(ErrorOf({"/um\xFA.org", "/p44405"}),
              "Main: /u: the host must be ASCII (a name with accents goes in punycode)");
}

// Q-C37: the original copied the host without looking at its length.
TEST(CommandLineWindowsTest, AcceptsAHostOf255BytesAndRejects256)
{
    EXPECT_EQ(ConnectOf({"/u" + std::string(255, 'a'), "/p44405"}).host.size(), 255u);
    EXPECT_EQ(ErrorOf({"/u" + std::string(256, 'a'), "/p44405"}), "Main: /u: the host must be 1 to 255 bytes");
}

// /y and /z, among others, are the launcher's and MuRet does not read them.
TEST(CommandLineWindowsTest, RejectsTheLauncherOptions)
{
    for (const std::string_view option : {"/y1", "/z1", "/Y"})
    {
        EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p44405", option}), "Main: unknown argument \"" + std::string(option) + "\"");
    }
}

TEST(CommandLineWindowsTest, RejectsWhatIsNotAnOption)
{
    EXPECT_EQ(ErrorOf({"foo"}), "Main: unknown argument \"foo\"");
    EXPECT_EQ(ErrorOf({"--fullscreen"}), "Main: unknown argument \"--fullscreen\"");
}

TEST(CommandLineWindowsTest, TheLauncherFormDoesNotGoWithHostOrPort)
{
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p44405", "/host", "10.0.0.2", "/port", "1"}),
              "Main: /u and /p do not go with --host and --port");
}

TEST(CommandLineWindowsTest, AfterTheDoubleDashTheLauncherFormIsIgnored)
{
    EXPECT_FALSE(Parsed({"--", "/u10.0.0.1", "/p44405"}).connect.has_value());
}

// SDL gives the arguments in UTF-8 (main.cpp uses SDL_main.h): a path that is not UTF-8 is an error, and the message
// does not repeat its bytes.
TEST(CommandLineWindowsTest, APathThatIsNotUtf8IsAnError)
{
    EXPECT_EQ(ErrorOf({"--data", "C:/MU/\xDA"}), "Main: --data needs a path in UTF-8");
    EXPECT_EQ(ErrorOf({"--config", "\xC0\xAF.ini"}), "Main: --config needs a path in UTF-8");
}
} // namespace
