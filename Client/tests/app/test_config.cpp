#include "App/Config.h"

#include "App/CommandLine.h"
#include "App/ConfigError.h"
#include "Kernel/LoopTiming.h"
#include "Kernel/PacingMode.h"
#include "Session/ServerAddress.h"
#include "Platform/VSyncMode.h"

#include "support/AppPrinters.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

using namespace std::string_view_literals;

// Client::App::ParseConfig (CL3.4), and at the end Resolve, which opens the file and merges the config with the
// command line.
namespace
{
using Client::App::ClientConfig;
using Client::App::CommandLine;
using Client::App::ConfigError;
using Client::App::EffectiveMaxFps;
using Client::App::HasServerHost;
using Client::App::MaxWindowSize;
using Client::App::ParseConfig;
using Client::App::Resolve;
using Client::Kernel::MaxFpsLimit;
using Client::Kernel::MinFpsLimit;
using Client::Kernel::PacingMode;
using Client::Platform::VSyncMode;
using Client::Session::DefaultConnectPort;
using Client::Session::MaxHostBytes;
using Client::Session::ServerAddress;

ConfigError ErrorOf(std::string_view text)
{
    const auto config = ParseConfig(text);
    EXPECT_FALSE(config.HasValue());
    return config.HasValue() ? ConfigError{} : config.Error();
}

TEST(ConfigTest, ReadsTheSections)
{
    const auto config = ParseConfig("[Data]\n"
                                    "Path = C:\\MU\\Data\n"
                                    "[Server]\n"
                                    "Host = 10.0.0.1\n"
                                    "Port = 55901\n"
                                    "[Video]\n"
                                    "Width = 800\n"
                                    "Height = 600\n"
                                    "VSync = adaptive\n"
                                    "Pacing = faithful\n"
                                    "[Log]\n"
                                    "Path = cliente.log\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().dataPath, std::filesystem::path("C:\\MU\\Data"));
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_EQ(config.Value().server->host, "10.0.0.1");
    EXPECT_EQ(config.Value().server->port, 55901);
    EXPECT_EQ(config.Value().width, 800);
    EXPECT_EQ(config.Value().height, 600);
    EXPECT_EQ(config.Value().vsync, VSyncMode::Adaptive);
    EXPECT_EQ(config.Value().pacing, PacingMode::Faithful);
    EXPECT_TRUE(config.Value().soundOnOff);
    EXPECT_TRUE(config.Value().musicOnOff);
    EXPECT_EQ(config.Value().logPath, std::filesystem::path("cliente.log"));
}

// Resolve's part (VSync on in normal mode and off in faithful) goes with its tests.
TEST(ConfigTest, DefaultsWhenAKeyIsMissing)
{
    const auto empty = ParseConfig("");
    ASSERT_TRUE(empty.HasValue());
    EXPECT_FALSE(empty.Value().dataPath.has_value());
    EXPECT_FALSE(empty.Value().server.has_value());
    EXPECT_EQ(empty.Value().width, 640);
    EXPECT_EQ(empty.Value().height, 480);
    EXPECT_TRUE(empty.Value().soundOnOff);
    EXPECT_TRUE(empty.Value().musicOnOff);
    EXPECT_FALSE(empty.Value().vsync.has_value());
    EXPECT_EQ(empty.Value().pacing, PacingMode::Decoupled);
    EXPECT_FALSE(empty.Value().logPath.has_value());

    const auto onlyHost = ParseConfig("[Server]\nHost = 10.0.0.1\n");
    ASSERT_TRUE(onlyHost.HasValue());
    ASSERT_TRUE(onlyHost.Value().server.has_value());
    EXPECT_EQ(onlyHost.Value().server->port, DefaultConnectPort);
    EXPECT_EQ(onlyHost.Value().server->port, 44405);

    // An empty host is not an error here: CL4 asks for it when connecting.
    const auto onlyPort = ParseConfig("[Server]\nPort = 55901\n");
    ASSERT_TRUE(onlyPort.HasValue());
    ASSERT_TRUE(onlyPort.Value().server.has_value());
    EXPECT_TRUE(onlyPort.Value().server->host.empty());
    EXPECT_EQ(onlyPort.Value().server->port, 55901);
}

TEST(ConfigTest, ReadsIndependentAudioPreferences)
{
    const auto config = ParseConfig("[Audio]\nSoundOnOff = 0\nMusicOnOff = 1\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_FALSE(config.Value().soundOnOff);
    EXPECT_TRUE(config.Value().musicOnOff);

    const auto bothOff = ParseConfig("[Audio]\nSoundOnOff = 0\nMusicOnOff = 0\n");
    ASSERT_TRUE(bothOff.HasValue());
    EXPECT_FALSE(bothOff.Value().soundOnOff);
    EXPECT_FALSE(bothOff.Value().musicOnOff);
}

TEST(ConfigTest, RejectsAnInvalidAudioPreference)
{
    const ConfigError error = ErrorOf("[Audio]\nSoundOnOff = true\n");

    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Audio] SoundOnOff: tiene que ser 0 o 1");
}

TEST(ConfigTest, ReportsTheLineOfAnError)
{
    const ConfigError error = ErrorOf("[Video]\nWidth = 800\nHeight 600\nVSync = on\n");

    EXPECT_EQ(error.line, 3u);
    EXPECT_NE(error.message.find("Clave = valor"), std::string::npos) << error.message;
}

TEST(ConfigTest, RejectsAnUnknownVSyncValue)
{
    const ConfigError error = ErrorOf("[Video]\nVSync = sometimes\n");

    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Video] VSync: tiene que ser on, adaptive u off");
}

// The config is UTF-8: on Windows, a path read as ANSI would come out wrong. It only tells them apart on Windows: on
// Linux, path(std::string) and path(std::u8string) give the same.
TEST(ConfigTest, ReadsAPathThatIsNotAscii)
{
    const auto config = ParseConfig("[Data]\nPath = C:\\Juegos\\M\xC3\x9A\n[Log]\nPath = registro-\xC3\xB1.log\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().dataPath, std::filesystem::path(u8"C:\\Juegos\\M\u00DA"));
    EXPECT_EQ(config.Value().logPath, std::filesystem::path(u8"registro-\u00F1.log"));
}

TEST(ConfigTest, RejectsAnUnknownPacingValue)
{
    const ConfigError error = ErrorOf("[Video]\nPacing = fast\n");

    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Video] Pacing: tiene que ser decoupled o faithful");
}

// The build chooses the server's packet shape (MU_XOR_REMAP): an .ini that still has the key says so, with its line.
TEST(ConfigTest, RejectsTheRemovedObfuscationKey)
{
    const ConfigError error = ErrorOf("[Server]\nObfuscation = foxanti\n");
    EXPECT_EQ(error.line, 2u);
    EXPECT_NE(error.message.find("MU_XOR_REMAP"), std::string::npos);
    EXPECT_EQ(ErrorOf("[server]\nobfuscation = none\n").message, error.message);
}

TEST(ConfigTest, SkipsCommentsBlankLinesAndTheBom)
{
    const auto config = ParseConfig("\xEF\xBB\xBF; muret-client.ini\r\n"
                                    "\r\n"
                                    "[Server] ; el ConnectServer\r\n"
                                    "  Host = 10.0.0.1   ; el del launcher\r\n"
                                    "Port = 55901;\r\n");

    // "55901;" is not a comment: the `;` has no space before it.
    ASSERT_FALSE(config.HasValue());
    EXPECT_EQ(config.Error().line, 5u);

    const auto fixed = ParseConfig("\xEF\xBB\xBF; muret-client.ini\r\n"
                                   "\r\n"
                                   "[Server] ; el ConnectServer\r\n"
                                   "  Host = 10.0.0.1   ; el del launcher\r\n"
                                   "Port = 55901 ;\r\n");
    ASSERT_TRUE(fixed.HasValue());
    EXPECT_EQ(fixed.Value().server->host, "10.0.0.1");
    EXPECT_EQ(fixed.Value().server->port, 55901);
}

TEST(ConfigTest, IgnoresTheCaseOfSectionsKeysAndModes)
{
    const auto config = ParseConfig("[VIDEO]\nvsync = OFF\nPACING = Faithful\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().vsync, VSyncMode::Off);
    EXPECT_EQ(config.Value().pacing, PacingMode::Faithful);
}

TEST(ConfigTest, RejectsARepeatedKey)
{
    const ConfigError error = ErrorOf("[Server]\nPort = 1\n[Video]\n[Server]\nport = 2\n");

    EXPECT_EQ(error.line, 5u);
    EXPECT_EQ(error.message, "[Server] Port está repetida");
}

TEST(ConfigTest, RejectsAnUnknownSectionOrKey)
{
    EXPECT_EQ(ErrorOf("[Sever]\nHost = 10.0.0.1\n").line, 1u);
    EXPECT_EQ(ErrorOf("[Server]\nHots = 10.0.0.1\n").line, 2u);
    EXPECT_EQ(ErrorOf("Host = 10.0.0.1\n").message, "Host está antes de la primera sección");
}

TEST(ConfigTest, RejectsAKeyWithoutAValue)
{
    const ConfigError error = ErrorOf("[Server]\nHost =\n");

    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Server] Host no tiene valor");
}

TEST(ConfigTest, RejectsAPortOutOfRange)
{
    EXPECT_EQ(ErrorOf("[Server]\nPort = 0\n").message, "[Server] Port: the port must be from 1 to 65535");
    EXPECT_EQ(ErrorOf("[Server]\nPort = 65536\n").message, "[Server] Port: the port must be from 1 to 65535");
    EXPECT_EQ(ParseConfig("[Server]\nPort = 65535\n").Value().server->port, 65535);
    EXPECT_EQ(ErrorOf("[Server]\nPort = 12a\n").message, "[Server] Port: the port must be a number");
    EXPECT_EQ(ErrorOf("[Server]\nPort = -1\n").message, "[Server] Port: the port must be a number");
}

// Q-C37: the original copied the host without checking its length.
TEST(ConfigTest, AcceptsAHostOf255BytesAndRejects256)
{
    const auto config = ParseConfig("[Server]\nHost = " + std::string(MaxHostBytes, 'a') + "\n");
    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().server->host.size(), 255u);

    const ConfigError error = ErrorOf("[Server]\nHost = " + std::string(MaxHostBytes + 1, 'a') + "\n");
    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Server] Host: the host must be 1 to 255 bytes");
}

TEST(ConfigTest, RejectsAHostWithSpaces)
{
    EXPECT_EQ(ErrorOf("[Server]\nHost = 10.0.0.1 x\n").message, "[Server] Host: the host cannot have spaces");
    EXPECT_EQ(ErrorOf("[Server]\nHost = a\tb\n").message, "[Server] Host: the host cannot have spaces");
}

TEST(ConfigTest, RejectsASizeOutOfRange)
{
    const auto largest = ParseConfig("[Video]\nWidth = 16384\nHeight = 1\n");
    ASSERT_TRUE(largest.HasValue());
    EXPECT_EQ(largest.Value().width, MaxWindowSize);
    EXPECT_EQ(largest.Value().height, 1);

    EXPECT_EQ(ErrorOf("[Video]\nWidth = 0\n").message, "[Video] Width: va de 1 a 16384");
    EXPECT_EQ(ErrorOf("[Video]\nWidth = 16385\n").message, "[Video] Width: va de 1 a 16384");
    EXPECT_EQ(ErrorOf("[Video]\nHeight = 2147483648\n").message, "[Video] Height: va de 1 a 16384");
    EXPECT_EQ(ErrorOf("[Video]\nHeight = 480px\n").message, "[Video] Height: tiene que ser un número");
    EXPECT_EQ(ErrorOf("[Video]\nHeight = -5\n").message, "[Video] Height: tiene que ser un número");
}

TEST(ConfigTest, ReadsEachVSyncAndPacingValue)
{
    EXPECT_EQ(ParseConfig("[Video]\nVSync = on\n").Value().vsync, VSyncMode::On);
    EXPECT_EQ(ParseConfig("[Video]\nVSync = adaptive\n").Value().vsync, VSyncMode::Adaptive);
    EXPECT_EQ(ParseConfig("[Video]\nVSync = off\n").Value().vsync, VSyncMode::Off);
    EXPECT_EQ(ParseConfig("[Video]\nPacing = faithful\n").Value().pacing, PacingMode::Faithful);
    // Decoupled is also the default: this checks that it is not an error.
    const auto decoupled = ParseConfig("[Video]\nPacing = decoupled\n");
    ASSERT_TRUE(decoupled.HasValue());
    EXPECT_EQ(decoupled.Value().pacing, PacingMode::Decoupled);
}

TEST(ConfigTest, ReadsTheLastLineWithoutANewline)
{
    const auto config = ParseConfig("[Video]\nWidth = 800");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().width, 800);
}

TEST(ConfigTest, TabsAreBlankAndSpacesInsideTheBracketsAreTrimmed)
{
    const auto config = ParseConfig("[ Server ]\n\tPort\t=\t1\t;\tc\n");

    ASSERT_TRUE(config.HasValue());
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_EQ(config.Value().server->port, 1);
}

TEST(ConfigTest, RemovesTheQuotesAroundAPathOrTheHost)
{
    const auto config = ParseConfig("[Data]\nPath = \"C:\\Program Files\\MU\"\n[Server]\nHost = \"10.0.0.1\"\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().dataPath, std::filesystem::path("C:\\Program Files\\MU"));
    EXPECT_EQ(config.Value().server->host, "10.0.0.1");

    const auto single = ParseConfig("[Log]\nPath = \"muret.log\n");
    ASSERT_TRUE(single.HasValue());
    EXPECT_EQ(single.Value().logPath, std::filesystem::path("\"muret.log"));
}

TEST(ConfigTest, ReadsAPathWithSpaces)
{
    const auto config = ParseConfig("[Data]\nPath = C:\\Program Files\\MU\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().dataPath, std::filesystem::path("C:\\Program Files\\MU"));
}

// An ini saved as ANSI: 0xDA is Windows-1252's Ú, and alone it is not UTF-8.
TEST(ConfigTest, RejectsAPathOrHostThatIsNotUtf8)
{
    const ConfigError path = ErrorOf("[Data]\nPath = C:\\M\xDA\n");
    EXPECT_EQ(path.line, 2u);
    EXPECT_EQ(path.message, "[Data] Path: no es UTF-8");

    // The host is not checked as UTF-8: it must be ASCII (ParseHost, test_server_address.cpp).
    EXPECT_EQ(ErrorOf("[Server]\nHost = \xDA\n").message,
              "[Server] Host: the host must be ASCII (a name with accents goes in punycode)");
    // An overlong form ("/" in two bytes) and a UTF-16 half (U+D800).
    EXPECT_EQ(ErrorOf("[Log]\nPath = \xC0\xAF\n").message, "[Log] Path: no es UTF-8");
    EXPECT_EQ(ErrorOf("[Log]\nPath = \xED\xA0\x80\n").message, "[Log] Path: no es UTF-8");
}

TEST(ConfigTest, RejectsControlCharacters)
{
    const ConfigError host = ErrorOf("[Server]\nHost = 10.0.0.1\0evil.com\n"sv);
    EXPECT_EQ(host.line, 2u);
    EXPECT_EQ(host.message, "[Server] Host: the host cannot have control characters");
    EXPECT_EQ(ErrorOf("[Server]\nHost = a\x7F\n").message, "[Server] Host: the host cannot have control characters");
    EXPECT_EQ(ErrorOf("[Log]\nPath = a\x7F\n").message, "[Log] Path: la ruta no puede tener caracteres de control");

    const ConfigError path = ErrorOf("[Data]\nPath = C:\\MU\0X\n"sv);
    EXPECT_EQ(path.line, 2u);
    EXPECT_EQ(path.message, "[Data] Path: la ruta no puede tener caracteres de control");
}

TEST(ConfigTest, RejectsACarriageReturnWithoutANewline)
{
    const ConfigError error = ErrorOf("[Server]\nPort = 1\n; x\r[Video]\rWidth = 0\r\n");

    EXPECT_EQ(error.line, 3u);
    EXPECT_EQ(error.message, "hay un \\r sin \\n: el fin de línea es \\n o \\r\\n");
}

TEST(ConfigTest, SaysWhatIsWrongWithALine)
{
    EXPECT_EQ(ErrorOf("[Server]\n= 5\n").message, "falta el nombre de la clave");
    EXPECT_EQ(ErrorOf("[Server\n").message, "a la sección le falta el ]");
    EXPECT_EQ(ErrorOf("[Server] x\n").message, "hay texto después del ] de la sección");
    // `#` is not a comment (inih accepts it; Config.h).
    const ConfigError hash = ErrorOf("# comentario\n");
    EXPECT_EQ(hash.line, 1u);
    EXPECT_EQ(hash.message, "se esperaba [Sección] o Clave = valor");
}

// The name carries a random number: the tests run in parallel, and other builds can run the same ones.
class TempFolder
{
public:
    TempFolder()
        : m_path(std::filesystem::temp_directory_path() / ("muret-config-" + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_path);
    }
    ~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const
    {
        return m_path;
    }

    std::filesystem::path Write(std::string_view text, std::string_view name = "muret-client.ini") const
    {
        const std::filesystem::path file = m_path / name;
        std::ofstream(file, std::ios::binary) << text;
        return file;
    }

private:
    std::filesystem::path m_path;
};

// Changes the current folder while it lives. It goes after the TempFolder it enters, so it leaves before that one is
// deleted (on Windows the current folder is not deleted).
class CurrentFolder
{
public:
    explicit CurrentFolder(const std::filesystem::path& folder) : m_previous(std::filesystem::current_path())
    {
        std::filesystem::current_path(folder);
    }
    ~CurrentFolder()
    {
        std::error_code error;
        std::filesystem::current_path(m_previous, error);
    }
    CurrentFolder(const CurrentFolder&) = delete;
    CurrentFolder& operator=(const CurrentFolder&) = delete;

private:
    std::filesystem::path m_previous;
};

TEST(ConfigTest, AMissingDefaultFileMeansDefaults)
{
    const TempFolder exeFolder;
    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    EXPECT_FALSE(config.Value().server.has_value());
    EXPECT_FALSE(config.Value().dataPath.has_value());
    EXPECT_EQ(config.Value().pacing, PacingMode::Decoupled);
    EXPECT_EQ(config.Value().vsync, VSyncMode::On);
}

TEST(ConfigTest, ResolveChoosesTheVSyncByThePacing)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nPacing = faithful\n");
    const auto faithful = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_TRUE(faithful.HasValue()) << faithful.Error();
    EXPECT_EQ(faithful.Value().vsync, VSyncMode::Off);

    exeFolder.Write("[Video]\nVSync = adaptive\n");
    const auto given = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_TRUE(given.HasValue()) << given.Error();
    EXPECT_EQ(given.Value().vsync, VSyncMode::Adaptive);
}

TEST(ConfigTest, AMissingConfigPathIsAnError)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.configPath = exeFolder.Path() / "no-esta.ini";

    const auto config = Resolve(exeFolder.Path(), commandLine);

    ASSERT_FALSE(config.HasValue());
    EXPECT_NE(config.Error().find("no existe la config"), std::string::npos) << config.Error();
    EXPECT_NE(config.Error().find("no-esta.ini"), std::string::npos) << config.Error();
}

TEST(ConfigTest, ReadsTheConfigPath)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.configPath = exeFolder.Write("[Video]\nWidth = 800\n", "otra.ini");

    const auto config = Resolve(exeFolder.Path(), commandLine);

    ASSERT_TRUE(config.HasValue()) << config.Error();
    EXPECT_EQ(config.Value().width, 800);
}

TEST(ConfigTest, AParseErrorNamesTheFileAndTheLine)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nWidth 800\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_FALSE(config.HasValue());
    EXPECT_NE(config.Error().find("muret-client.ini, línea 2: "), std::string::npos) << config.Error();
}

TEST(ConfigTest, TheCommandLineWins)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Server]\nHost = 10.0.0.1\nPort = 44405\n[Video]\nPacing = decoupled\n");
    CommandLine commandLine;
    commandLine.connect = ServerAddress{"192.168.0.2", 55901};
    commandLine.pacing = PacingMode::Faithful;

    const auto config = Resolve(exeFolder.Path(), commandLine);

    ASSERT_TRUE(config.HasValue()) << config.Error();
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_EQ(config.Value().server->host, "192.168.0.2");
    EXPECT_EQ(config.Value().server->port, 55901);
    EXPECT_EQ(config.Value().pacing, PacingMode::Faithful);
    EXPECT_EQ(config.Value().vsync, VSyncMode::Off);

    // --vsync off overrides a VSync = on that faithful mode does not allow.
    exeFolder.Write("[Video]\nPacing = faithful\nVSync = on\n");
    CommandLine vsyncOff;
    vsyncOff.vsync = VSyncMode::Off;
    const auto fixed = Resolve(exeFolder.Path(), vsyncOff);
    ASSERT_TRUE(fixed.HasValue()) << fixed.Error();
    EXPECT_EQ(fixed.Value().vsync, VSyncMode::Off);
}

// --config and --data are taken from the current folder, like any argument.
TEST(ConfigTest, ACommandLinePathIsFromTheCurrentFolder)
{
    const TempFolder exeFolder;
    const TempFolder other;
    std::filesystem::create_directories(other.Path() / "Data");
    other.Write("[Video]\nWidth = 800\n", "otra.ini");
    // It enters the temp folder instead of computing the path with relative(): on Windows the temp folder can be on
    // another drive than the current folder, and there relative() gives an empty path.
    const CurrentFolder current(other.Path());
    CommandLine commandLine;
    commandLine.configPath = std::filesystem::path("otra.ini");
    commandLine.dataPath = std::filesystem::path("Data");

    const auto resolved = Resolve(exeFolder.Path(), commandLine);

    ASSERT_TRUE(resolved.HasValue()) << resolved.Error();
    EXPECT_EQ(resolved.Value().width, 800);
    EXPECT_EQ(resolved.Value().dataPath, commandLine.dataPath);
}

TEST(ConfigTest, AConfigPathThatIsAFolderIsAnError)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.configPath = exeFolder.Path();
    const auto folder = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(folder.HasValue());
    EXPECT_NE(folder.Error().find("no es un archivo"), std::string::npos) << folder.Error();

    // A muret-client.ini folder next to the executable, too.
    std::filesystem::create_directories(exeFolder.Path() / "muret-client.ini");
    const auto byDefault = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_FALSE(byDefault.HasValue());
    EXPECT_NE(byDefault.Error().find("no es un archivo"), std::string::npos) << byDefault.Error();
}

TEST(ConfigTest, AConfigLongerThanTheCapIsAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write(std::string(Client::App::MaxConfigBytes, ';'));
    ASSERT_TRUE(Resolve(exeFolder.Path(), CommandLine{}).HasValue());

    exeFolder.Write(std::string(Client::App::MaxConfigBytes + 1, ';'));
    const auto config = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_FALSE(config.HasValue());
    EXPECT_NE(config.Error().find("pasa de 65536 bytes"), std::string::npos) << config.Error();
}

// Resolve's order: the file, ParseConfig, the faithful mode rule and only then the folder.
TEST(ConfigTest, ResolveStopsAtTheFirstErrorInOrder)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.dataPath = exeFolder.Path() / "Data-que-no-esta";

    exeFolder.Write("[Video]\nWidth 800\n");
    const auto parse = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(parse.HasValue());
    EXPECT_NE(parse.Error().find("línea 2"), std::string::npos) << parse.Error();

    exeFolder.Write("[Video]\nPacing = faithful\nVSync = on\n");
    const auto faithful = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(faithful.HasValue());
    EXPECT_NE(faithful.Error().find("Pacing"), std::string::npos) << faithful.Error();
}

TEST(ConfigTest, FaithfulWithVSyncOnIsAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nPacing = faithful\nVSync = on\n");
    const auto fromConfig = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_FALSE(fromConfig.HasValue());
    EXPECT_NE(fromConfig.Error().find("Pacing"), std::string::npos) << fromConfig.Error();
    EXPECT_NE(fromConfig.Error().find("VSync"), std::string::npos) << fromConfig.Error();

    // The config asks for VSync and the command line for faithful mode: the same error.
    exeFolder.Write("[Video]\nVSync = adaptive\n");
    CommandLine commandLine;
    commandLine.pacing = PacingMode::Faithful;
    const auto mixed = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(mixed.HasValue());
    EXPECT_EQ(mixed.Error(), fromConfig.Error());

    // Only with the command line (the CL3.2 rule): the message names both options.
    const TempFolder empty;
    for (const VSyncMode vsync : {VSyncMode::On, VSyncMode::Adaptive})
    {
        CommandLine both;
        both.pacing = PacingMode::Faithful;
        both.vsync = vsync;
        const auto config = Resolve(empty.Path(), both);
        ASSERT_FALSE(config.HasValue());
        EXPECT_NE(config.Error().find("--pacing faithful"), std::string::npos) << config.Error();
        EXPECT_NE(config.Error().find("--vsync"), std::string::npos) << config.Error();
    }
    CommandLine off;
    off.pacing = PacingMode::Faithful;
    off.vsync = VSyncMode::Off;
    EXPECT_TRUE(Resolve(empty.Path(), off).HasValue());
    CommandLine decoupled;
    decoupled.pacing = PacingMode::Decoupled;
    decoupled.vsync = VSyncMode::On;
    EXPECT_TRUE(Resolve(empty.Path(), decoupled).HasValue());
}

// The 02 test and Q-C35: the original exited without a warning if it did not find its files.
TEST(ConfigTest, AMissingDataFolderNamesIt)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.dataPath = exeFolder.Path() / "Data-que-no-esta";

    const auto config = Resolve(exeFolder.Path(), commandLine);

    ASSERT_FALSE(config.HasValue());
    EXPECT_NE(config.Error().find("no existe la carpeta de datos"), std::string::npos) << config.Error();
    EXPECT_NE(config.Error().find("Data-que-no-esta"), std::string::npos) << config.Error();

    // A file is not a folder, and it says so.
    commandLine.dataPath = exeFolder.Write("[Video]\n");
    const auto file = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(file.HasValue());
    EXPECT_NE(file.Error().find("no es una carpeta"), std::string::npos) << file.Error();
}

TEST(ConfigTest, ARelativePathIsFromTheExeFolder)
{
    const TempFolder exeFolder;
    std::filesystem::create_directories(exeFolder.Path() / "Data");
    exeFolder.Write("[Data]\nPath = Data\n[Log]\nPath = muret.log\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    EXPECT_EQ(config.Value().dataPath, exeFolder.Path() / "Data");
    EXPECT_EQ(config.Value().logPath, exeFolder.Path() / "muret.log");
}

TEST(ConfigTest, NoDataFolderIsNotAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nWidth = 800\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    EXPECT_FALSE(config.Value().dataPath.has_value());
}

// [Video] FontFile and BoldFontFile (question 8 of the CL5 plan): FreeType's fonts, paths like [Data]'s. Without them
// they stay empty; what the client does without FontFile is CL5.3's search in Platform, not the config's.
TEST(ConfigTest, ReadsTheFontFiles)
{
    const auto config = ParseConfig("[Video]\nFontFile = \"fuentes/letra.ttf\"\nBoldFontFile = negrita.ttf\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().fontFile, std::filesystem::path("fuentes/letra.ttf"));
    EXPECT_EQ(config.Value().boldFontFile, std::filesystem::path("negrita.ttf"));

    const auto empty = ParseConfig("");
    ASSERT_TRUE(empty.HasValue());
    EXPECT_FALSE(empty.Value().fontFile.has_value());
    EXPECT_FALSE(empty.Value().boldFontFile.has_value());

    const auto onlyRegular = ParseConfig("[Video]\nFontFile = letra.ttf\n");
    ASSERT_TRUE(onlyRegular.HasValue());
    EXPECT_TRUE(onlyRegular.Value().fontFile.has_value());
    EXPECT_FALSE(onlyRegular.Value().boldFontFile.has_value());

    EXPECT_EQ(ErrorOf("[Video]\nFontFile = \"\"\n").message, "[Video] FontFile: la ruta está vacía");
}

// Like [Data] and [Log]: from the executable's folder. Resolve does not check that they exist: FreeTypeRasterizer::Open
// gives the error of a missing file, with the path.
TEST(ConfigTest, ARelativeFontFileIsFromTheExeFolder)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nFontFile = letra.ttf\nBoldFontFile = negrita.ttf\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    EXPECT_EQ(config.Value().fontFile, exeFolder.Path() / "letra.ttf");
    EXPECT_EQ(config.Value().boldFontFile, exeFolder.Path() / "negrita.ttf");
}

// CL4 asks for it, when connecting.
TEST(ConfigTest, NoHostIsNotAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Server]\nPort = 55901\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_TRUE(config.Value().server->host.empty());
}

// Main connects only with a host (the CL4 plan, CL4.4): a [Server] with only the port is not enough.
TEST(ConfigTest, OnlyThePortIsNotAServerHost)
{
    const auto config = ParseConfig("[Server]\nPort = 55901\n");

    ASSERT_TRUE(config.HasValue());
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_FALSE(HasServerHost(config.Value()));
}

// Without [Server], or with an empty host, neither.
TEST(ConfigTest, AnEmptyHostIsNotAServerHost)
{
    ClientConfig config;
    EXPECT_FALSE(HasServerHost(config));

    config.server = ServerAddress{"", DefaultConnectPort};
    EXPECT_FALSE(HasServerHost(config));
}

TEST(ConfigTest, AHostIsAServerHost)
{
    const auto config = ParseConfig("[Server]\nHost = 10.0.0.1\n");

    ASSERT_TRUE(config.HasValue());
    EXPECT_TRUE(HasServerHost(config.Value()));
}

// --auto-login (CL5.5) is for entering a server: without a host, from the config or from /u /p, there is nothing to
// enter, and Resolve says so before opening the window.
TEST(ConfigTest, AutoLoginNeedsAServer)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.autoLogin = true;

    const auto withoutHost = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(withoutHost.HasValue());
    EXPECT_NE(withoutHost.Error().find("--auto-login needs a server"), std::string::npos) << withoutHost.Error();

    exeFolder.Write("[Server]\nHost = 10.0.0.1\n");
    EXPECT_TRUE(Resolve(exeFolder.Path(), commandLine).HasValue());

    const TempFolder empty;
    commandLine.connect = ServerAddress{"10.0.0.2", 44405};
    EXPECT_TRUE(Resolve(empty.Path(), commandLine).HasValue());
}

// [Video] MaxFps, the normal mode's FPS cap: 0 (no cap, the default), or 25 to 1000.
TEST(ConfigTest, ReadsMaxFps)
{
    const auto defaults = ParseConfig("");
    ASSERT_TRUE(defaults.HasValue());
    EXPECT_EQ(defaults.Value().maxFps, 0);
    for (const int maxFps : {0, MinFpsLimit, 144, MaxFpsLimit})
    {
        const auto config = ParseConfig("[Video]\nMaxFps = " + std::to_string(maxFps) + "\n");
        ASSERT_TRUE(config.HasValue()) << maxFps;
        EXPECT_EQ(config.Value().maxFps, maxFps);
    }
}

// 1 would last MaxFrameTime and always be clamped: the lowest is MinFpsLimit.
TEST(ConfigTest, RejectsAMaxFpsOutOfRange)
{
    for (const std::string_view maxFps : {"1001", "99999999999", "1", "24"})
    {
        EXPECT_EQ(ErrorOf("[Video]\nMaxFps = " + std::string(maxFps) + "\n").message,
                  "[Video] MaxFps: va de 25 a 1000, o 0 (sin tope)")
            << maxFps;
    }
    EXPECT_EQ(ErrorOf("[Video]\nMaxFps = -1\n").message, "[Video] MaxFps: tiene que ser un número");
    EXPECT_EQ(ErrorOf("[Video]\nMaxFps = 60fps\n").message, "[Video] MaxFps: tiene que ser un número");
}

// --max-fps overrides the config, also with 0 (no cap).
TEST(ConfigTest, TheCommandLineMaxFpsWins)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nMaxFps = 144\n");
    const auto fromConfig = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_TRUE(fromConfig.HasValue()) << fromConfig.Error();
    EXPECT_EQ(fromConfig.Value().maxFps, 144);

    for (const int maxFps : {60, 0})
    {
        CommandLine commandLine;
        commandLine.maxFps = maxFps;
        const auto resolved = Resolve(exeFolder.Path(), commandLine);
        ASSERT_TRUE(resolved.HasValue()) << resolved.Error();
        EXPECT_EQ(resolved.Value().maxFps, maxFps);
    }
}

// Faithful mode waits like the original (46.875 ms per frame): an FPS cap is an error and names both options.
// MaxFps = 0 is fine.
TEST(ConfigTest, FaithfulWithAnFpsCapIsAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nPacing = faithful\nMaxFps = 30\n");
    const auto fromConfig = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_FALSE(fromConfig.HasValue());
    EXPECT_NE(fromConfig.Error().find("Pacing"), std::string::npos) << fromConfig.Error();
    EXPECT_NE(fromConfig.Error().find("MaxFps"), std::string::npos) << fromConfig.Error();

    const TempFolder empty;
    CommandLine both;
    both.pacing = PacingMode::Faithful;
    both.maxFps = 144;
    const auto fromCommandLine = Resolve(empty.Path(), both);
    ASSERT_FALSE(fromCommandLine.HasValue());
    EXPECT_NE(fromCommandLine.Error().find("--max-fps"), std::string::npos) << fromCommandLine.Error();

    both.maxFps = 0;
    const auto withoutCap = Resolve(empty.Path(), both);
    ASSERT_TRUE(withoutCap.HasValue()) << withoutCap.Error();
    EXPECT_EQ(withoutCap.Value().maxFps, 0);

    // Mixed: the mode from the config and the cap from the command line, and the other way round.
    const TempFolder faithfulConfig;
    faithfulConfig.Write("[Video]\nPacing = faithful\n");
    CommandLine capOnly;
    capOnly.maxFps = 60;
    EXPECT_FALSE(Resolve(faithfulConfig.Path(), capOnly).HasValue());
    const TempFolder capConfig;
    capConfig.Write("[Video]\nMaxFps = 60\n");
    CommandLine faithfulOnly;
    faithfulOnly.pacing = PacingMode::Faithful;
    EXPECT_FALSE(Resolve(capConfig.Path(), faithfulOnly).HasValue());
}

// Like MuMain's FpsCap: with VSync the swap already paces to the refresh.
TEST(ConfigTest, TheFpsCapIsOnlyForVSyncOff)
{
    EXPECT_EQ(EffectiveMaxFps(60, VSyncMode::Off), 60);
    EXPECT_EQ(EffectiveMaxFps(60, VSyncMode::On), 0);
    EXPECT_EQ(EffectiveMaxFps(60, VSyncMode::Adaptive), 0);
    EXPECT_EQ(EffectiveMaxFps(0, VSyncMode::Off), 0);
}
} // namespace
