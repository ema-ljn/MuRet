#pragma once

#include <string_view>

// Mayúsculas y minúsculas de ASCII, byte a byte: solo de la A a la Z, como compara Windows un
// nombre de archivo en ASCII (fuera de ASCII Windows junta más letras, y esto no). Así compara el
// cliente las rutas de Data\ (Data/DataPath.h), las extensiones de las texturas, la lista de
// fuentes del registro, las claves y los valores de la config y las palabras de la línea de
// comandos. Cada uno decide qué compara y qué dice si no coincide; la regla es esta sola.
//
// std::tolower no sirve: depende del locale de C y es comportamiento indefinido con un char
// negativo, como cualquier byte >= 0x80 de un texto en UTF-8 o en la página de códigos de Windows.
// Esos bytes se comparan tal cual.
namespace Client::Core
{
// `c` en minúscula si es de la A a la Z; si no, `c` sin cambios.
[[nodiscard]] char ToLowerAscii(char c);

// Si `a` y `b` tienen el mismo tamaño y son iguales byte a byte, con ToLowerAscii de los dos lados.
// Un '\0' adentro es un byte más.
[[nodiscard]] bool EqualsIgnoringAsciiCase(std::string_view a, std::string_view b);
} // namespace Client::Core
