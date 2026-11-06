#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

// Leer archivos de Data\ enteros o como prefijo, para los cargadores que reciben bytes: los
// juegos de texturas, Text.bmd y los 2.304 bytes de Gate.bmd. Y el error de un cargador en palabras.
namespace Client::Data
{
// Todos los bytes de `path`, o el error del sistema. Un archivo de más de `maxBytes` no es del
// formato que se pide y no se lee: std::errc::file_too_large. Si se achica mientras se lee, queda
// lo que se leyó: el cargador mira el largo.
[[nodiscard]] Core::Expected<std::vector<char>, std::error_code> ReadDataFile(const std::filesystem::path& path,
                                                                              std::uintmax_t maxBytes);

// Hasta los primeros byteCount bytes, sin leer ni limitar la cola. Un EOF devuelve los bytes
// disponibles: el cargador decide si alcanzan para el formato (Gate.bmd pide solo 2.304). Si la
// cantidad no cabe en std::streamsize o en el contenedor, devuelve std::errc::value_too_large antes
// de cualquier E/S o reserva. El caller limita byteCount al tamaño de su formato.
[[nodiscard]] Core::Expected<std::vector<char>, std::error_code> ReadDataFilePrefix(const std::filesystem::path& path,
                                                                                    std::size_t byteCount);

// Lo que dice cada DataError, sin bytes del archivo.
[[nodiscard]] std::string_view DescribeDataError(DataError error);
} // namespace Client::Data
