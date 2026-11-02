#include "App/CommandLine.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

// Client::App::ParseCommandLine: los argumentos de Main.
namespace
{
using Client::App::CommandLine;
using Client::App::ParseCommandLine;

std::optional<CommandLine> Parse(std::vector<std::string_view> arguments, std::string& error)
{
    return ParseCommandLine(arguments, error);
}

TEST(CommandLineTest, NoArgumentsMeansNoFrameLimit)
{
    std::string error;
    const auto commandLine = Parse({}, error);

    ASSERT_TRUE(commandLine.has_value());
    EXPECT_FALSE(commandLine->maxFrames.has_value());
}

TEST(CommandLineTest, FramesTakesAPositiveNumber)
{
    std::string error;
    const auto commandLine = Parse({"--frames", "60"}, error);

    ASSERT_TRUE(commandLine.has_value());
    EXPECT_EQ(commandLine->maxFrames, 60u);
}

TEST(CommandLineTest, FramesWithoutANumberSaysSo)
{
    std::string error;

    EXPECT_FALSE(Parse({"--frames"}, error).has_value());
    EXPECT_NE(error.find("--frames necesita un número"), std::string::npos) << error;
}

TEST(CommandLineTest, FramesRejectsWhatIsNotAPositiveNumber)
{
    for (const std::string_view value : {"0", "-1", "+5", "12x", "", " 7", "18446744073709551616"})
    {
        std::string error;
        EXPECT_FALSE(Parse({"--frames", value}, error).has_value()) << '"' << value << '"';
        EXPECT_NE(error.find("mayor que 0"), std::string::npos) << error;
    }
}

TEST(CommandLineTest, UnknownArgumentsAreAnError)
{
    std::string error;

    EXPECT_FALSE(Parse({"--fullscreen"}, error).has_value());
    EXPECT_NE(error.find("\"--fullscreen\""), std::string::npos) << error;
}
} // namespace
