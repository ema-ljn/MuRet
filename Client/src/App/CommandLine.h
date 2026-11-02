#pragma once

#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Los argumentos de Main.
//
//   Main                  corre hasta que se cierre la ventana
//   Main --frames N       dibuja N frames y sale (para probarlo sin nadie que cierre la ventana)
//   Main --pacing MODO    decoupled (el de MuRet) o faithful (el ritmo del original), CL3.2
//   Main --vsync MODO     on, adaptive u off; en el modo fiel solo off, como el original
//   Main --max-fps N      el tope de FPS del modo normal, de 0 (sin tope) a MaxFpsLimit
//   Main --stats          imprime los FPS y los pasos por segundo cada 5 s
//   Main --config RUTA    lee esa config en vez de muret-client.ini junto al ejecutable (CL3.4)
//   Main --data RUTA      la carpeta de datos, en vez de [Data] Path
//   Main connect /u<host> /p<puerto>
//                         el servidor, como lo pasa el launcher (02 § Del launcher al juego):
//                         connect es opcional, /u y /p van en cualquier orden y sin mirar
//                         mayúsculas, y uno sin el otro es error. Pisan [Server] de la config
namespace Client::App
{
struct CommandLine
{
    // Sin --frames, vacío: sin límite.
    std::optional<std::uint64_t> maxFrames;
    // Sin --pacing, vacío: lo decide la config (CL3.4), y si no, Decoupled.
    std::optional<PacingMode> pacing;
    // Sin --vsync, vacío: lo decide el modo (CL3.4), On en Decoupled y Off en Faithful.
    std::optional<VSyncMode> vsync;
    // Sin --max-fps, vacío: lo decide la config, y si no, 0 (sin tope). --max-fps 0 pisa un MaxFps
    // de la config.
    std::optional<int> maxFps;
    bool stats = false;
    // /u<host> /p<puerto>, con o sin connect adelante. Vacío si no se dio ninguno.
    std::optional<ServerAddress> connect;
    // --config y --data. Relativas, desde la carpeta actual: son de quien lanza el cliente.
    std::optional<std::filesystem::path> configPath;
    std::optional<std::filesystem::path> dataPath;
};

// Lee los argumentos, sin el nombre del programa (argv[1..]). Si alguno está mal, devuelve el
// mensaje para el usuario. Las combinaciones (el modo fiel sin VSync) las mira Resolve (Config.h).
Core::Expected<CommandLine, std::string> ParseCommandLine(std::span<const std::string_view> arguments);
} // namespace Client::App
