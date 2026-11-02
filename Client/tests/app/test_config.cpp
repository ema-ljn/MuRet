#include "App/Config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

// Client::App::ParseConfig: el texto de muret-client.ini (CL3.4). Resolve, que abre el archivo y
// junta la config con la línea de comandos, tiene sus tests más abajo cuando entre.
namespace
{
using Client::App::ConfigError;
using Client::App::DefaultConnectPort;
using Client::App::MaxHostBytes;
using Client::App::MaxWindowSize;
using Client::App::PacingMode;
using Client::App::ParseConfig;
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
} // namespace
