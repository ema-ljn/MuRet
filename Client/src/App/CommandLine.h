// Los argumentos de Main.
//
//   Main              corre hasta que se cierre la ventana
//   Main --frames N   dibuja N frames y sale (para probarlo sin nadie que cierre la ventana)

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::App
{

struct CommandLine
{
    // Sin --frames, vacío: sin límite.
    std::optional<std::uint64_t> maxFrames;
};

// Lee los argumentos, sin el nombre del programa (argv[1..]). Si alguno está mal, devuelve
// std::nullopt y deja en `error` el mensaje para el usuario.
std::optional<CommandLine> ParseCommandLine(std::span<const std::string_view> arguments, std::string& error);

} // namespace Client::App
