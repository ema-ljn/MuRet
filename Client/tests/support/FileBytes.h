#pragma once

#include <filesystem>
#include <optional>
#include <vector>

// Leer un archivo entero, para los tests golden. El producto no lee del disco en la capa de datos:
// sus cargadores reciben los bytes.
namespace Client::Tests
{
// Todos los bytes de `path`, o std::nullopt si no es un archivo común (una carpeta, por ejemplo) o
// no se pudo leer entero. Van como char: un cargador los recibe como std::as_bytes(std::span(*file)).
//
// Lee tantos bytes como el tamaño que informa el sistema, así que sirve para archivos que no
// cambian mientras se leen, como los golden. Uno de /proc informa 0 bytes y se lee vacío.
std::optional<std::vector<char>> ReadFileBytes(const std::filesystem::path& path);
} // namespace Client::Tests
