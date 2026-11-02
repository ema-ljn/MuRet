#pragma once

#include <filesystem>
#include <string>

// Una ruta como texto UTF-8, para un mensaje (el log, un error del arranque). path::string() en
// Windows la pasa a la página de códigos del sistema, y una letra que no está ahí ("ñ" en otra
// página, un nombre en japonés) tira una excepción.
namespace Client::Core
{
[[nodiscard]] std::string PathText(const std::filesystem::path& path);
} // namespace Client::Core
