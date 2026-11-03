#pragma once

#include "App/CommandLine.h"
#include "App/ConfigError.h"
#include "App/ObfuscationMode.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// La config del cliente, muret-client.ini (CL3.4; 02 § Qué implica para MuRet). El original lee el
// registro (HKCU\SOFTWARE\Webzen\Mu\Config) y un config.ini; MuRet, solo este archivo:
//
//   [Data]
//   Path = C:\MU\Data          ; la carpeta de datos (sin darla, la que diga Data::DataPath)
//   [Server]
//   Host = 10.0.0.1            ; de 1 a 255 bytes y sin espacios (Q-C37)
//   Port = 44405               ; de 1 a 65535
//   Obfuscation = foxanti      ; foxanti (la del laboratorio) o none
//   [Video]
//   Width = 640
//   Height = 480
//   VSync = on                 ; on, adaptive u off
//   Pacing = decoupled         ; decoupled o faithful
//   MaxFps = 0                 ; tope de FPS con VSync off: 0 (sin tope), o de MinFpsLimit a
//                              ; MaxFpsLimit
//   FontFile = letra.ttf       ; la fuente TrueType del texto (CL5.3)
//   BoldFontFile = negrita.ttf ; su negrita; sin darla, se ensancha la de FontFile
//   [Log]
//   Path = muret-client.log
//
// Las secciones, las claves y los valores de Obfuscation, VSync y Pacing no distinguen
// mayúsculas. Un `;` al principio de la línea, o después de un espacio, empieza un comentario; `#`
// no es comentario, y una línea que empieza con `#` es error. Se acepta el BOM de UTF-8 y el fin de
// línea de Windows. Un par de comillas dobles alrededor de una ruta o del host se saca (lo que da
// "Copiar como ruta" del Explorador); una comilla sola queda como parte del valor. Es error, y
// dice su línea: una línea que no es `[Sección]` ni `Clave = valor`, una sección o una clave que no
// existen, una clave repetida o sin valor, un valor que no se puede leer, un \r sin \n, una ruta
// con caracteres de control o que no es UTF-8, y un host que no es ASCII visible (ParseHost).
//
// El parser es propio y no inih (CODING_RULES §10; paso 1 de la CL3.4 en el plan de la CL3). El
// puerto de vcpkg (inih 62) compila con las opciones por defecto de su meson. Lo que lo descarta:
// el largo de la línea se fija al compilar (max_line_length = 200) y el portfile no deja
// cambiarlo, así que una línea de más de 198 bytes sin contar el fin de línea es error, y un
// `Host = ` con 255 bytes (Q-C37) no entra por ninguna de sus dos API. Además, una línea con
// sangría sigue el valor de la clave anterior (multi-line_entries), y el INIReader de la feature
// `cpp` junta las claves repetidas con "\n" y no da la línea de un valor malo; esas dos se
// podrían rechazar desde el handler de la API de C, pero no la primera.
namespace Client::App
{
// El lado más grande de la ventana que acepta [Video]. No sale del original: con 16384, Width *
// Height entra en un int (2^28), y sobra para un monitor de hoy (el 8K es 7680 × 4320).
inline constexpr int MaxWindowSize = 16384;
// El tamaño más grande de config que lee Resolve. No sale del original: una config entera tiene
// menos de 1 KiB, y con el tope un archivo enorme o un dispositivo (/dev/zero) no se lee entero.
inline constexpr std::uintmax_t MaxConfigBytes = 64 * 1024;
// El archivo que Resolve busca junto al ejecutable si no hay --config (tabla de la CL3.4).
inline constexpr std::string_view DefaultConfigFileName = "muret-client.ini";

struct ClientConfig
{
    // [Data] Path. Vacío: no se dio, y vale la carpeta por defecto.
    std::optional<std::filesystem::path> dataPath;
    // [Server] Host y Port. Vacío si no está ninguno de los dos; con uno solo, el otro queda en su
    // valor por defecto (host vacío, puerto DefaultConnectPort).
    std::optional<ServerAddress> server;
    // [Server] Obfuscation (pregunta 8 del plan de la CL4). No arma `server`: es de la conexión,
    // no de la dirección, y /u /p no la cambia.
    ObfuscationMode obfuscation = ObfuscationMode::FoxAnti;
    // [Video] Width y Height, de 1 a MaxWindowSize; 640 × 480 por defecto (la tabla de la CL3.4 en
    // el plan de la CL3). Para abrir la ventana tienen que ser una resolución que valga
    // (App/Resolution.h): eso lo mira main.cpp, con los modos del monitor, después de SDL_Init.
    int width = 640;
    int height = 480;
    // [Video] VSync. Vacío: lo decide Resolve (paso 4), según el Pacing.
    std::optional<VSyncMode> vsync;
    // [Video] Pacing.
    PacingMode pacing = PacingMode::Decoupled;
    // [Video] MaxFps, 0 o de MinFpsLimit a MaxFpsLimit: el tope de FPS del modo normal (el FpsCap
    // de MuMain), que solo se usa con VSync off (EffectiveMaxFps). 0, el valor por defecto, es sin
    // tope, como MuMain.
    int maxFps = 0;
    // [Video] FontFile y BoldFontFile: las fuentes TrueType del texto (pregunta 8 del plan de la
    // CL5). Vacías: no se dieron. Sin BoldFontFile, la negrita es la de FontFile ensanchada.
    std::optional<std::filesystem::path> fontFile;
    std::optional<std::filesystem::path> boldFontFile;
    // [Log] Path. Vacío: muret-client.log junto al ejecutable.
    std::optional<std::filesystem::path> logPath;
};

// Lee el texto de muret-client.ini. No abre el archivo ni mira si las rutas existen: eso lo hace
// Resolve. El primer error corta.
Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text);

// La config que usa Main, en este orden (el primer error corta):
//   1. el archivo: el de --config, que tiene que existir, o DefaultConfigFileName en exeFolder, y
//      si ese no está, los valores por defecto (así Main --frames 60 anda sin nada);
//   2. ParseConfig: un error nombra el archivo y la línea. Una ruta relativa de la config (la
//      carpeta de datos, el log y las fuentes) se toma desde exeFolder, no desde la carpeta actual;
//   3. la línea de comandos pisa la config (/u /p, --data, --pacing, --vsync, --max-fps);
//   4. en el modo fiel, un VSync que no es off es error (nombra Pacing y VSync), y sin VSync
//      queda en off; un MaxFps que no es 0, también (nombra Pacing y MaxFps): el modo fiel
//      espera como el original. En el normal, sin VSync queda en on. --auto-login sin un host
//      (HasServerHost) también es error (CL5.5);
//   5. la carpeta de datos, solo si se dio, tiene que existir.
// Sin --auto-login, el host vacío no es error acá: Main sigue sin red.
Core::Expected<ClientConfig, std::string> Resolve(const std::filesystem::path& exeFolder,
                                                  const CommandLine& commandLine);

// Si la config da a qué conectarse: un host que no está vacío. Un [Server] con solo el puerto no
// alcanza, y Main sigue sin red (el plan de la CL4, CL4.4).
[[nodiscard]] bool HasServerHost(const ClientConfig& config);

// El tope de FPS que usa el bucle: `maxFps` solo con el VSync que quedó en la ventana apagado, y
// si no, 0 (sin tope), como el FpsCap de MuMain (ApplyVSyncPreferenceNow en Winmain.cpp). Con
// VSync el swap ya frena al refresco, y un tope que no lo divide (50 en 60 Hz) dibujaría a saltos.
[[nodiscard]] int EffectiveMaxFps(int maxFps, VSyncMode vsync);
} // namespace Client::App
