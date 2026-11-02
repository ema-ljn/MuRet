#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Las rutas y el texto UTF-8. path(std::string) y path::string() en Windows usan la página de
// códigos del sistema (la ANSI), no UTF-8: una letra que no está ahí ("ñ" en otra página, un nombre
// en japonés) se cambia por "?" o tira una excepción. Estas dos van por char8_t, que path lee y
// escribe como UTF-8 en todos lados.
namespace Client::Core
{
// Una ruta como texto UTF-8, para un mensaje (el log, un error del arranque).
[[nodiscard]] std::string PathText(const std::filesystem::path& path);

// La ruta que dice un texto UTF-8 (la config, la línea de comandos), o vacío si el texto no es
// UTF-8 bien formado: sin bytes sueltos, sin formas más largas de lo necesario, sin las mitades
// de UTF-16 (U+D800 a U+DFFF) y sin pasar de U+10FFFF. Es lo que rechaza MultiByteToWideChar con
// MB_ERR_INVALID_CHARS, que usa path en Windows: con un byte malo tiraría una excepción.
[[nodiscard]] std::optional<std::filesystem::path> PathFromUtf8(std::string_view text);
} // namespace Client::Core
