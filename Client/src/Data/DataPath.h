#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

// Rutas de Data\ como las escribe el original, resueltas en cualquier sistema.
//
// main.exe pide sus archivos con rutas de Windows ("Data\\Player\\Player.bmd"), y Windows no
// distingue mayúsculas. Linux sí, y la copia del Data\ del laboratorio tiene carpetas en minúscula
// (player\, skill\, sound\): sin esto, en Linux la mitad de los archivos "no estaría"
// (Client/docs/research/09-datos-disponibles.md, "Cómo se verificó").
namespace Client::Data
{
// Si `a` y `b` son iguales sin distinguir mayúsculas ASCII (solo de la A a la Z), como compara
// Windows los nombres: la regla con que ResolveDataPath compara cada parte, y la de las
// extensiones de Textures/TextureSet.cpp. Los bytes >= 0x80 se comparan tal cual.
[[nodiscard]] bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b);

// Busca `originalPath` (un archivo o una carpeta), separado por '\\' o '/', adentro de `dataRoot`.
// Cada parte se compara sin distinguir mayúsculas ASCII, como en Windows: si hay una coincidencia
// exacta gana esa, y si no, la primera en orden de bytes, sin volver atrás si la siguiente parte
// no está en ella (solo pasa en Linux, con dos carpetas que difieren en mayúsculas). Los bytes
// >= 0x80 (nombres en UTF-8) se comparan tal cual.
//
// `originalPath` se lee como una cadena de C: termina en el primer '\0', como los nombres de
// largo fijo de los .bmd. Partes vacías ("a\\\\b") y "." se ignoran.
//
// Devuelve:
//   - la ruta, si existe. Abre el archivo, pero su grafía depende del sistema: en Linux lleva los
//     nombres del disco; en Windows, que no distingue mayúsculas, puede quedar como se pidió;
//   - std::nullopt con `error` vacío, si no existe (un symlink roto no existe) o si la ruta no es
//     de algo adentro de Data\: vacía, solo ".", absoluta ("\\x", "C:\\x"), terminada en
//     separador o con "..";
//   - std::nullopt con `error`, si no se pudo leer una carpeta (no existe `dataRoot`, permisos,
//     falla de E/S) o si `dataRoot` está vacío (std::errc::invalid_argument).
std::optional<std::filesystem::path> ResolveDataPath(const std::filesystem::path& dataRoot,
                                                     std::string_view originalPath, std::error_code& error);
} // namespace Client::Data
