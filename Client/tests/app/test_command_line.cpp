#include "App/CommandLine.h"

#include "Core/PathText.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Client::App::ParseCommandLine: los argumentos de Main.
namespace
{
using Client::App::CommandLine;
using Client::App::PacingMode;
using Client::App::ParseCommandLine;
using Client::App::ServerAddress;
using Client::App::VSyncMode;

// Los tests de antes de la CL3.4 miran un optional y el mensaje aparte.
std::optional<CommandLine> Parse(std::vector<std::string_view> arguments, std::string& error)
{
    auto parsed = ParseCommandLine(arguments);
    if (!parsed.HasValue())
    {
        error = parsed.Error();
        return std::nullopt;
    }
    return std::move(parsed).Value();
}

// El mensaje de error, o "" si no hubo.
std::string ErrorOf(std::vector<std::string_view> arguments)
{
    const auto parsed = ParseCommandLine(arguments);
    return parsed.HasValue() ? std::string() : parsed.Error();
}

ServerAddress ConnectOf(std::vector<std::string_view> arguments)
{
    const auto parsed = ParseCommandLine(arguments);
    EXPECT_TRUE(parsed.HasValue()) << (parsed.HasValue() ? "" : parsed.Error());
    if (!parsed.HasValue() || !parsed.Value().connect)
    {
        ADD_FAILURE() << "sin connect";
        return {};
    }
    return *parsed.Value().connect;
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

// --auto-login (CL5.5): sin valor, como --stats. Sin él, la cuenta la teclea el jugador.
TEST(CommandLineTest, ParsesAutoLogin)
{
    std::string error;
    EXPECT_FALSE(Parse({}, error).value().autoLogin) << error;

    const auto commandLine = Parse({"--auto-login", "connect", "/u10.0.0.1", "/p44405"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_TRUE(commandLine->autoLogin);
    ASSERT_TRUE(commandLine->connect.has_value());
}

// En el modo fiel VSync va apagado, como el original: el mensaje nombra las dos opciones.
// La regla del modo fiel sin VSync la mira Resolve (ConfigTest.FaithfulWithVSyncOnIsAnError).
TEST(CommandLineTest, LeavesTheFaithfulRuleToResolve)
{
    std::string error;
    EXPECT_TRUE(Parse({"--pacing", "faithful", "--vsync", "on"}, error).has_value()) << error;
}

TEST(CommandLineTest, AcceptsConnectWithHostAndPort)
{
    const ServerAddress address = ConnectOf({"connect", "/u10.0.0.1", "/p44405"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 44405);
}

TEST(CommandLineTest, IgnoresTheCase)
{
    const ServerAddress address = ConnectOf({"CONNECT", "/U10.0.0.1", "/P44405"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 44405);
}

TEST(CommandLineTest, AcceptsAnyOrder)
{
    const ServerAddress address = ConnectOf({"/p55901", "--frames", "60", "/u10.0.0.1"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 55901);
}

TEST(CommandLineTest, ConnectIsOptional)
{
    const ServerAddress address = ConnectOf({"/u10.0.0.1", "/p44405"});

    EXPECT_EQ(address.host, "10.0.0.1");
    EXPECT_EQ(address.port, 44405);
}

TEST(CommandLineTest, ConnectAloneIsIgnored)
{
    std::string error;
    const auto commandLine = Parse({"connect"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_FALSE(commandLine->connect.has_value());
}

TEST(CommandLineTest, RejectsAPortOutOfRange)
{
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1", "/p0"}), "el puerto va de 1 a 65535");
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1", "/p65536"}), "el puerto va de 1 a 65535");
    EXPECT_EQ(ConnectOf({"/u10.0.0.1", "/p65535"}).port, 65535);
    EXPECT_EQ(ConnectOf({"/u10.0.0.1", "/p1"}).port, 1);
}

TEST(CommandLineTest, RejectsAPortWithLetters)
{
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p12a"}), "el puerto tiene que ser un número");
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p"}), "/p necesita el puerto pegado (/p44405)");
}

TEST(CommandLineTest, RejectsHostWithoutPort)
{
    EXPECT_EQ(ErrorOf({"connect", "/u10.0.0.1"}), "/u necesita /p con el puerto");
}

TEST(CommandLineTest, RejectsPortWithoutHost)
{
    EXPECT_EQ(ErrorOf({"/p44405"}), "/p necesita /u con el host");
}

TEST(CommandLineTest, RejectsAnEmptyHost)
{
    EXPECT_EQ(ErrorOf({"/u", "/p44405"}), "/u necesita el host pegado (/u10.0.0.1)");
    EXPECT_EQ(ErrorOf({"/u", "10.0.0.1", "/p44405"}), "/u necesita el host pegado (/u10.0.0.1)");
}

TEST(CommandLineTest, RejectsARepeatedHostOrPort)
{
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/U10.0.0.2", "/p1"}), "/U está repetido");
    EXPECT_EQ(ErrorOf({"/u10.0.0.1", "/p1", "/p2"}), "/p está repetido");
}

TEST(CommandLineTest, RejectsARepeatedOption)
{
    EXPECT_EQ(ErrorOf({"--frames", "5", "--frames", "7"}), "--frames está repetido");
    EXPECT_EQ(ErrorOf({"--config", "a.ini", "--config", "b.ini"}), "--config está repetido");
    EXPECT_EQ(ErrorOf({"--data", "a", "--data", "b"}), "--data está repetido");
    EXPECT_EQ(ErrorOf({"--pacing", "faithful", "--pacing", "decoupled"}), "--pacing está repetido");
    EXPECT_EQ(ErrorOf({"--vsync", "off", "--vsync", "on"}), "--vsync está repetido");
    EXPECT_EQ(ErrorOf({"--stats", "--stats"}), "--stats está repetido");
    EXPECT_EQ(ErrorOf({"--auto-login", "--auto-login"}), "--auto-login está repetido");
}

// Q-C37, en la línea de comandos: el mismo ParseHost que la config.
TEST(CommandLineTest, RejectsAHostThatIsNotAscii)
{
    EXPECT_EQ(ErrorOf({"/um\xFA.org", "/p44405"}),
              "el host tiene que ser ASCII (un nombre con acentos va en punycode)");
}

TEST(CommandLineTest, RejectsTheLauncherOptions)
{
    for (const std::string_view option : {"/y1", "/z1", "/Y"})
    {
        const std::string error = ErrorOf({"/u10.0.0.1", "/p44405", option});
        EXPECT_NE(error.find("no conozco el argumento \"" + std::string(option) + "\""), std::string::npos) << error;
    }
}

// Q-C37: el original copiaba el host sin mirar el largo.
TEST(CommandLineTest, AcceptsAHostOf255Bytes)
{
    const std::string option = "/u" + std::string(255, 'a');

    EXPECT_EQ(ConnectOf({option, "/p44405"}).host.size(), 255u);
}

TEST(CommandLineTest, RejectsAHostOf256Bytes)
{
    const std::string option = "/u" + std::string(256, 'a');

    EXPECT_EQ(ErrorOf({option, "/p44405"}), "el host va de 1 a 255 bytes");
}

TEST(CommandLineTest, ParsesConfigPath)
{
    std::string error;
    const auto commandLine = Parse({"--config", "otra.ini"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_EQ(commandLine->configPath, std::filesystem::path("otra.ini"));
    EXPECT_FALSE(Parse({"--config"}, error).has_value());
    EXPECT_NE(error.find("--config necesita una ruta"), std::string::npos) << error;
}

TEST(CommandLineTest, ParsesDataPath)
{
    std::string error;
    const auto commandLine = Parse({"--data", "C:/MU/Data"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_EQ(commandLine->dataPath, std::filesystem::path("C:/MU/Data"));
    EXPECT_FALSE(commandLine->configPath.has_value());
    EXPECT_FALSE(Parse({"--data"}, error).has_value());
    EXPECT_NE(error.find("--data necesita una ruta"), std::string::npos) << error;
}

// Los argumentos llegan en UTF-8 (main.cpp usa SDL_main.h, que en Windows los arma con
// GetCommandLineW): la ruta se lee como UTF-8 y no en la página ANSI. "Ł" (U+0141) no está en la
// 1252, y con path(std::string) en Windows llegaría como "?".
TEST(CommandLineTest, ReadsAPathAsUtf8)
{
    std::string error;
    const auto commandLine = Parse({"--data", "C:/Juegos/\xC5\x81ukasz/Data", "--config", "Espa\xC3\xB1ol.ini"}, error);

    ASSERT_TRUE(commandLine.has_value()) << error;
    EXPECT_EQ(Client::Core::PathText(*commandLine->dataPath), "C:/Juegos/\xC5\x81ukasz/Data");
    EXPECT_EQ(Client::Core::PathText(*commandLine->configPath), "Espa\xC3\xB1ol.ini");
}

// Un texto que no es UTF-8: en POSIX son los bytes del sistema de archivos (un nombre guardado
// en Latin-1) y pasan tal cual; en Windows, donde SDL ya los dio en UTF-8, es un error, y el
// mensaje no repite los bytes. Cada CI corre su rama: Linux la primera, Windows la segunda.
TEST(CommandLineTest, APathThatIsNotUtf8IsBytesOnPosixAndAnErrorOnWindows)
{
    if constexpr (std::is_same_v<std::filesystem::path::value_type, char>)
    {
        std::string error;
        const auto commandLine = Parse({"--data", "/mu/\xDA", "--config", "\xC0\xAF.ini"}, error);
        ASSERT_TRUE(commandLine.has_value()) << error;
        // Las dos ramas compilan en las dos plataformas (fuera de un template, if constexpr no
        // es un #if): por eso path y no native(), que en Windows es wstring.
        EXPECT_EQ(commandLine->dataPath, std::filesystem::path("/mu/\xDA"));
        EXPECT_EQ(commandLine->configPath, std::filesystem::path("\xC0\xAF.ini"));
    }
    else
    {
        EXPECT_EQ(ErrorOf({"--data", "C:/MU/\xDA"}), "--data necesita una ruta en UTF-8");
        EXPECT_EQ(ErrorOf({"--config", "\xC0\xAF.ini"}), "--config necesita una ruta en UTF-8");
    }
}

// --max-fps N: 0 (sin tope, pisa la config), o de 25 a 1000.
TEST(CommandLineTest, ParsesMaxFps)
{
    std::string error;
    for (const auto& [text, maxFps] :
         {std::pair{"144", 144}, std::pair{"0", 0}, std::pair{"25", 25}, std::pair{"1000", 1000}})
    {
        const auto commandLine = Parse({"--max-fps", text}, error);
        ASSERT_TRUE(commandLine.has_value()) << error;
        EXPECT_EQ(commandLine->maxFps, maxFps);
    }
    const auto without = Parse({}, error);
    ASSERT_TRUE(without.has_value()) << error;
    EXPECT_FALSE(without->maxFps.has_value());
}

TEST(CommandLineTest, RejectsABadMaxFps)
{
    // Debajo de MinFpsLimit algunos frames correrían dos pasos; 4294967296 no entra en un unsigned.
    for (const std::string_view text : {"1001", "1", "24", "-1", "-0", "+5", "60fps", "", "4294967296", "99999999999"})
    {
        EXPECT_EQ(ErrorOf({"--max-fps", text}),
                  "--max-fps necesita 0 o un número de 25 a 1000, no \"" + std::string(text) + "\"")
            << text;
    }
    EXPECT_NE(ErrorOf({"--max-fps"}).find("--max-fps necesita un número"), std::string::npos);
    EXPECT_EQ(ErrorOf({"--max-fps", "60", "--max-fps", "30"}), "--max-fps está repetido");
}
} // namespace
