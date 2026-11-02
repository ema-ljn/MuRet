#include "App/Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

// Client::App::ParseConfig: el texto de muret-client.ini (CL3.4). Y Resolve, que abre el archivo y
// junta la config con la línea de comandos (al final).
namespace
{
using Client::App::CommandLine;
using Client::App::ConfigError;
using Client::App::DefaultConnectPort;
using Client::App::MaxHostBytes;
using Client::App::MaxWindowSize;
using Client::App::PacingMode;
using Client::App::ParseConfig;
using Client::App::Resolve;
using Client::App::ServerAddress;
using Client::App::VSyncMode;

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
    EXPECT_EQ(config.Value().logPath, std::filesystem::path("cliente.log"));
}

// Lo de Resolve (VSync on en el modo normal y off en el fiel) va con sus tests.
TEST(ConfigTest, DefaultsWhenAKeyIsMissing)
{
    const auto empty = ParseConfig("");
    ASSERT_TRUE(empty.HasValue());
    EXPECT_FALSE(empty.Value().dataPath.has_value());
    EXPECT_FALSE(empty.Value().server.has_value());
    EXPECT_EQ(empty.Value().width, 640);
    EXPECT_EQ(empty.Value().height, 480);
    EXPECT_FALSE(empty.Value().vsync.has_value());
    EXPECT_EQ(empty.Value().pacing, PacingMode::Decoupled);
    EXPECT_FALSE(empty.Value().logPath.has_value());

    const auto onlyHost = ParseConfig("[Server]\nHost = 10.0.0.1\n");
    ASSERT_TRUE(onlyHost.HasValue());
    ASSERT_TRUE(onlyHost.Value().server.has_value());
    EXPECT_EQ(onlyHost.Value().server->port, DefaultConnectPort);
    EXPECT_EQ(onlyHost.Value().server->port, 44405);

    // El host vacío no es error acá: lo pide la CL4 al conectarse.
    const auto onlyPort = ParseConfig("[Server]\nPort = 55901\n");
    ASSERT_TRUE(onlyPort.HasValue());
    ASSERT_TRUE(onlyPort.Value().server.has_value());
    EXPECT_TRUE(onlyPort.Value().server->host.empty());
    EXPECT_EQ(onlyPort.Value().server->port, 55901);
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

// La config es UTF-8: en Windows, una ruta leída como ANSI quedaría mal. Solo distingue en
// Windows: en Linux, path(std::string) y path(std::u8string) dan lo mismo.
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

TEST(ConfigTest, SkipsCommentsBlankLinesAndTheBom)
{
    const auto config = ParseConfig("\xEF\xBB\xBF; muret-client.ini\r\n"
                                    "\r\n"
                                    "[Server] ; el ConnectServer\r\n"
                                    "  Host = 10.0.0.1   ; el del launcher\r\n"
                                    "Port = 55901;\r\n");

    // "55901;" no es un comentario: el `;` no tiene un espacio adelante.
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
    // Una clave antes de la primera sección no es de ninguna.
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
    EXPECT_EQ(ErrorOf("[Server]\nPort = 0\n").message, "[Server] Port: el puerto va de 1 a 65535");
    EXPECT_EQ(ErrorOf("[Server]\nPort = 65536\n").message, "[Server] Port: el puerto va de 1 a 65535");
    EXPECT_EQ(ErrorOf("[Server]\nPort = 12a\n").message, "[Server] Port: el puerto tiene que ser un número");
    EXPECT_EQ(ErrorOf("[Server]\nPort = -1\n").message, "[Server] Port: el puerto tiene que ser un número");
}

// Q-C37: el original copiaba el host sin mirar el largo.
TEST(ConfigTest, AcceptsAHostOf255BytesAndRejects256)
{
    const auto config = ParseConfig("[Server]\nHost = " + std::string(MaxHostBytes, 'a') + "\n");
    ASSERT_TRUE(config.HasValue());
    EXPECT_EQ(config.Value().server->host.size(), 255u);

    const ConfigError error = ErrorOf("[Server]\nHost = " + std::string(MaxHostBytes + 1, 'a') + "\n");
    EXPECT_EQ(error.line, 2u);
    EXPECT_EQ(error.message, "[Server] Host: el host va de 1 a 255 bytes");
}

TEST(ConfigTest, RejectsAHostWithSpaces)
{
    EXPECT_EQ(ErrorOf("[Server]\nHost = 10.0.0.1 x\n").message, "[Server] Host: el host no puede tener espacios");
    EXPECT_EQ(ErrorOf("[Server]\nHost = a\tb\n").message, "[Server] Host: el host no puede tener espacios");
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
    // Decoupled es también el valor por defecto: se mira que no sea error.
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

    // Una comilla sola es parte del valor.
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

// Un ini guardado como ANSI: 0xDA es la Ú de Windows-1252, y sola no es UTF-8.
TEST(ConfigTest, RejectsAPathOrHostThatIsNotUtf8)
{
    const ConfigError path = ErrorOf("[Data]\nPath = C:\\M\xDA\n");
    EXPECT_EQ(path.line, 2u);
    EXPECT_EQ(path.message, "[Data] Path: no es UTF-8");

    EXPECT_EQ(ErrorOf("[Server]\nHost = \xDA\n").message, "[Server] Host: no es UTF-8");
    // Una forma larga de más ("/" en dos bytes) y una mitad de UTF-16 (U+D800).
    EXPECT_EQ(ErrorOf("[Log]\nPath = \xC0\xAF\n").message, "[Log] Path: no es UTF-8");
    EXPECT_EQ(ErrorOf("[Log]\nPath = \xED\xA0\x80\n").message, "[Log] Path: no es UTF-8");
}

TEST(ConfigTest, RejectsControlCharacters)
{
    const ConfigError host = ErrorOf("[Server]\nHost = 10.0.0.1\0evil.com\n"sv);
    EXPECT_EQ(host.line, 2u);
    EXPECT_EQ(host.message, "[Server] Host: el host no puede tener caracteres de control");
    EXPECT_EQ(ErrorOf("[Server]\nHost = a\x7F\n").message,
              "[Server] Host: el host no puede tener caracteres de control");
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
    // `#` no es comentario (inih lo acepta; Config.h).
    const ConfigError hash = ErrorOf("# comentario\n");
    EXPECT_EQ(hash.line, 1u);
    EXPECT_EQ(hash.message, "se esperaba [Sección] o Clave = valor");
}
// Una carpeta nueva en el temporal, que se borra al final. El nombre lleva un número al azar: los
// tests corren en paralelo, y otras compilaciones pueden correr los mismos.
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

    // Escribe muret-client.ini (u otro nombre) con ese texto y devuelve su ruta.
    std::filesystem::path Write(std::string_view text, std::string_view name = "muret-client.ini") const
    {
        const std::filesystem::path file = m_path / name;
        std::ofstream(file, std::ios::binary) << text;
        return file;
    }

private:
    std::filesystem::path m_path;
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
}

TEST(ConfigTest, FaithfulWithVSyncOnIsAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Video]\nPacing = faithful\nVSync = on\n");
    const auto fromConfig = Resolve(exeFolder.Path(), CommandLine{});
    ASSERT_FALSE(fromConfig.HasValue());
    EXPECT_NE(fromConfig.Error().find("Pacing"), std::string::npos) << fromConfig.Error();
    EXPECT_NE(fromConfig.Error().find("VSync"), std::string::npos) << fromConfig.Error();

    // La config pide VSync y la línea de comandos el modo fiel: es el mismo error.
    exeFolder.Write("[Video]\nVSync = adaptive\n");
    CommandLine commandLine;
    commandLine.pacing = PacingMode::Faithful;
    const auto mixed = Resolve(exeFolder.Path(), commandLine);
    ASSERT_FALSE(mixed.HasValue());
    EXPECT_EQ(mixed.Error(), fromConfig.Error());
}

// El test del 02 y Q-C35: el original salía sin avisar si no encontraba lo suyo.
TEST(ConfigTest, AMissingDataFolderNamesIt)
{
    const TempFolder exeFolder;
    CommandLine commandLine;
    commandLine.dataPath = exeFolder.Path() / "Data-que-no-esta";

    const auto config = Resolve(exeFolder.Path(), commandLine);

    ASSERT_FALSE(config.HasValue());
    EXPECT_NE(config.Error().find("no existe la carpeta de datos"), std::string::npos) << config.Error();
    EXPECT_NE(config.Error().find("Data-que-no-esta"), std::string::npos) << config.Error();
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

// Lo pide la CL4, al conectarse.
TEST(ConfigTest, NoHostIsNotAnError)
{
    const TempFolder exeFolder;
    exeFolder.Write("[Server]\nPort = 55901\n");

    const auto config = Resolve(exeFolder.Path(), CommandLine{});

    ASSERT_TRUE(config.HasValue()) << config.Error();
    ASSERT_TRUE(config.Value().server.has_value());
    EXPECT_TRUE(config.Value().server->host.empty());
}
} // namespace
