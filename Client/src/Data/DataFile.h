#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

// Leer un archivo de Data\ entero, para los cargadores de la CL1, que reciben los bytes y no abren
// archivos: los juegos de texturas (Textures/TextureSet.cpp, CL5.1) y Text.bmd
// (Scenes/GameData.cpp, CL5.4). Y el error de un cargador en palabras, para el log.
namespace Client::Data
{
// Todos los bytes de `path`, o el error del sistema. Un archivo de más de `maxBytes` no es del
// formato que se pide y no se lee: std::errc::file_too_large. Si se achica mientras se lee, queda
// lo que se leyó: el cargador mira el largo.
[[nodiscard]] Core::Expected<std::vector<char>, std::error_code> ReadDataFile(const std::filesystem::path& path,
                                                                              std::uintmax_t maxBytes);

// Lo que dice cada DataError, sin bytes del archivo.
[[nodiscard]] std::string_view DescribeDataError(DataError error);
} // namespace Client::Data
