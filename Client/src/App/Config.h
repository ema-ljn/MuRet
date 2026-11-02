#pragma once

#include "App/ConfigError.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/Expected.h"

#include <filesystem>
#include <optional>
#include <string_view>

// La config del cliente, muret-client.ini (CL3.4; 02 § Qué implica para MuRet). El original lee el
// registro (HKCU\SOFTWARE\Webzen\Mu\Config) y un config.ini; MuRet, solo este archivo:
//
//   [Data]
//   Path = C:\MU\Data          ; la carpeta de datos (sin darla, la que diga Data::DataPath)
//   [Server]
//   Host = 10.0.0.1            ; de 1 a 255 bytes y sin espacios (Q-C37)
//   Port = 44405               ; de 1 a 65535
//   [Video]
//   Width = 640
//   Height = 480
//   VSync = on                 ; on, adaptive u off
//   Pacing = decoupled         ; decoupled o faithful
//   [Log]
//   Path = muret-client.log
//
// Las secciones, las claves y los valores de VSync y Pacing no distinguen mayúsculas. Un `;` al
// principio de la línea, o después de un espacio, empieza un comentario. Se acepta el BOM de UTF-8
// y el fin de línea de Windows. Es error, y dice su línea: una línea que no es `[Sección]` ni
// `Clave = valor`, una sección o una clave que no existen, una clave repetida o sin valor, y un
// valor que no se puede leer.
//
// El parser es propio y no inih (CODING_RULES §10; paso 1 de la CL3.4 en el plan de la CL3). El
// puerto de vcpkg (inih 62) compila con las opciones por defecto de su meson, y tres no sirven:
// una línea de más de 199 bytes es error (max_line_length = 200, y el host llega a 255 bytes), una
// línea con sangría sigue el valor de la clave anterior (multi-line_entries), y el INIReader de la
// feature `cpp` junta las claves repetidas con "\n" y no da la línea de un valor malo. Con la API
// de C se puede rechazar todo eso desde el handler, pero ini_parse da una sola línea para el
// primer error de sintaxis y el primero del handler; separarlos lleva dos pasadas, más código que
// este parser.
namespace Client::App
{
struct ClientConfig
{
    // [Data] Path. Vacío: no se dio, y vale la carpeta por defecto.
    std::optional<std::filesystem::path> dataPath;
    // [Server] Host y Port. Vacío si no está ninguno de los dos; con uno solo, el otro queda en su
    // valor por defecto (host vacío, puerto DefaultConnectPort).
    std::optional<ServerAddress> server;
    // [Video] Width y Height, 640 × 480 por defecto (la tabla de la CL3.4 en el plan de la CL3).
    int width = 640;
    int height = 480;
    // [Video] VSync. Vacío: lo decide Resolve (paso 4), según el Pacing.
    std::optional<VSyncMode> vsync;
    // [Video] Pacing.
    PacingMode pacing = PacingMode::Decoupled;
    // [Log] Path. Vacío: muret-client.log junto al ejecutable.
    std::optional<std::filesystem::path> logPath;
};

// Lee el texto de muret-client.ini. No abre el archivo ni mira si las rutas existen: eso lo hace
// Resolve. El primer error corta.
Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text);
} // namespace Client::App
