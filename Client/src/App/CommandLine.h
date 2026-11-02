#pragma once

#include "App/PacingMode.h"
#include "App/VSyncMode.h"

#include <cstdint>
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
//   Main --stats          imprime los FPS y los pasos por segundo cada 5 s
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
    bool stats = false;
};

// Lee los argumentos, sin el nombre del programa (argv[1..]). Si alguno está mal, o si piden el
// modo fiel con un VSync que no es off, devuelve std::nullopt y deja en `error` el mensaje para el
// usuario.
std::optional<CommandLine> ParseCommandLine(std::span<const std::string_view> arguments, std::string& error);
} // namespace Client::App
