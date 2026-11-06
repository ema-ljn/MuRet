#pragma once

#include "Core/Expected.h"
#include "Data/Tables/GateTable.h"
#include "Data/Text/TextTable.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Lo que se lee una vez, antes de la primera escena, y usan las que siguen (CL5.4). Vive en
// main.cpp, como el SceneManager.
namespace Client::Scenes
{
// Text.bmd como lo pide el original ("Data\Local\Text.bmd", 0x6d4370).
inline constexpr std::string_view TextTableFile = "Local\\Text.bmd";
// Los dos filtros del nombre, como los pide 0x61ccb0 (0x6d2a7c y 0x6d2ab8).
inline constexpr std::string_view FilterNameFile = "Local\\FilterName.bmd";
inline constexpr std::string_view FilterFile = "Local\\Filter.bmd";
// La tabla de 0x54a360, pedida por 0x61ccb0 en la carga (05 § Gate.bmd).
inline constexpr std::string_view GateTableFile = "Gate.bmd";

struct GameData
{
    // Text.bmd (CL1): los textos se piden por índice. La cara de la fuente es el texto 0.
    std::optional<Data::Text::TextTable> text;
    // FilterName.bmd y Filter.bmd (CL6.5): las palabras que no puede tener el nombre de un personaje
    // nuevo (UI::NameFilters).
    std::vector<std::string> filterName;
    std::vector<std::string> filter;
    std::optional<Data::Tables::GateTable> gates;
};

// Lo de WinMain antes de la escena 1: Text.bmd en 0x4b9b9f (0x61ee30 y 0x5495e0), antes de las
// fuentes (la cara es su texto 0) y de la carga. Lo busca en `dataRoot` sin mirar mayúsculas
// (ResolveDataPath). Si falta, no se lee o no carga, un error que lo nombra, sin un texto. El
// original lee igual un archivo corto y deja lo que había en la memoria (05, Tablas .bmd). Después,
// los dos filtros, que 0x61ccb0 lee en la carga: uno que no está queda vacío (el original no
// arranca sin él), y uno que no se lee o cuya suma no da es un error que lo nombra. Gate.bmd es
// obligatorio: se carga con sus 256 registros; un error de ruta, lectura o formato lo nombra.
[[nodiscard]] Core::Expected<GameData, std::string> LoadGameData(const std::filesystem::path& dataRoot);
} // namespace Client::Scenes
