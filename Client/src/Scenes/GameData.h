#pragma once

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Lo que se lee una vez, antes de la primera escena, y usan las que siguen (CL5.4). Vive en
// main.cpp, como el SceneManager.
namespace Client::Scenes
{
// Text.bmd como lo pide el original ("Data\Local\Text.bmd", 0x6d4370).
inline constexpr std::string_view TextTableFile = "Local\\Text.bmd";

struct GameData
{
    // Text.bmd (CL1): los textos se piden por índice. La cara de la fuente es el texto 0.
    std::optional<Data::Text::TextTable> text;
};

// Lo de WinMain antes de la escena 1: Text.bmd en 0x4b9b9f (0x61ee30 y 0x5495e0), antes de las
// fuentes (la cara es su texto 0) y de la carga. Lo busca en `dataRoot` sin mirar mayúsculas
// (ResolveDataPath). Si falta, no se lee o no carga, un error que lo nombra, sin un texto. El
// original lee igual un archivo corto y deja lo que había en la memoria (05, Tablas .bmd).
[[nodiscard]] Core::Expected<GameData, std::string> LoadGameData(const std::filesystem::path& dataRoot);
} // namespace Client::Scenes
