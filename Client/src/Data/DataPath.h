// Rutas de Data\ como las escribe el original, resueltas en cualquier sistema.
//
// main.exe pide sus archivos con rutas de Windows ("Data\\Player\\Player.bmd"), y Windows no
// distingue mayúsculas. Linux sí, y la copia del Data\ del laboratorio tiene carpetas en minúscula
// (player\, skill\, sound\): sin esto, en Linux la mitad de los archivos "no estaría"
// (Client/docs/research/09-datos-disponibles.md, "Cómo se verificó").

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace Client::Data
{

// Busca `originalPath`, separado por '\\' o '/', adentro de `dataRoot`. Cada parte se compara sin
// distinguir mayúsculas ASCII, como en Windows: si hay una coincidencia exacta gana esa, y si no,
// la primera en orden de bytes. Devuelve std::nullopt si no existe, si la ruta está vacía, si es
// absoluta o si sube con "..".
std::optional<std::filesystem::path> ResolveDataPath(const std::filesystem::path& dataRoot,
                                                     std::string_view originalPath);

} // namespace Client::Data
