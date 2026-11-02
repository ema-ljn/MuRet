#include "App/CommandLine.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

// Client::App::ParseCommandLine: los argumentos de Main.
namespace
{
using Client::App::CommandLine;
using Client::App::PacingMode;
using Client::App::ParseCommandLine;
using Client::App::VSyncMode;

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
    EXPECT_FALSE(commandLine->pacing.has_value());
    EXPECT_FALSE(commandLine->vsync.has_value());
    EXPECT_FALSE(commandLine->stats);
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
TEST(CommandLineTest, ParsesPacing)
{
    std::string error;
    EXPECT_EQ(Parse({"--pacing", "decoupled"}, error).value().pacing, PacingMode::Decoupled) << error;
    EXPECT_EQ(Parse({"--pacing", "faithful"}, error).value().pacing, PacingMode::Faithful) << error;

    for (const std::string_view value : {"Faithful", "fiel", ""})
    {
        EXPECT_FALSE(Parse({"--pacing", value}, error).has_value()) << '"' << value << '"';
        EXPECT_NE(error.find("decoupled o faithful"), std::string::npos) << error;
    }
    EXPECT_FALSE(Parse({"--pacing"}, error).has_value());
    EXPECT_NE(error.find("--pacing necesita un valor"), std::string::npos) << error;
}

TEST(CommandLineTest, ParsesVSync)
{
    std::string error;
    EXPECT_EQ(Parse({"--vsync", "on"}, error).value().vsync, VSyncMode::On) << error;
    EXPECT_EQ(Parse({"--vsync", "adaptive"}, error).value().vsync, VSyncMode::Adaptive) << error;
    EXPECT_EQ(Parse({"--vsync", "off"}, error).value().vsync, VSyncMode::Off) << error;

    EXPECT_FALSE(Parse({"--vsync", "1"}, error).has_value());
    EXPECT_NE(error.find("on, adaptive u off"), std::string::npos) << error;
}

TEST(CommandLineTest, ParsesStats)
{
    std::string error;
    const auto commandLine = Parse({"--stats", "--frames", "60"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_TRUE(commandLine->stats);
    EXPECT_EQ(commandLine->maxFrames, 60u);
}

// En el modo fiel VSync va apagado, como el original: el mensaje nombra las dos opciones.
TEST(CommandLineTest, FaithfulWithVSyncOnIsAnError)
{
    for (const std::string_view vsync : {"on", "adaptive"})
    {
        std::string error;
        EXPECT_FALSE(Parse({"--pacing", "faithful", "--vsync", vsync}, error).has_value()) << vsync;
        EXPECT_NE(error.find("--pacing faithful"), std::string::npos) << error;
        EXPECT_NE(error.find("--vsync"), std::string::npos) << error;
    }

    std::string error;
    EXPECT_TRUE(Parse({"--vsync", "off", "--pacing", "faithful"}, error).has_value()) << error;
    EXPECT_TRUE(Parse({"--pacing", "faithful"}, error).has_value()) << error;
    EXPECT_TRUE(Parse({"--pacing", "decoupled", "--vsync", "on"}, error).has_value()) << error;
}
} // namespace
