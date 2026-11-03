#pragma once

#include <optional>
#include <string>

// Las variables de entorno del proceso: las de los tests golden (tests/support/GoldenDir.cpp) y
// las de la cuenta y la clave (App/Credentials.h). Es el único lugar de ClientLib, fuera de los
// sockets, que distingue el compilador: std::getenv da C4996 en MSVC, que con /WX no compila.
namespace Client::Core
{
// La variable `name`, o std::nullopt si no está. `name` termina en \0. En MSVC va por _dupenv_s,
// que devuelve una copia en el heap: se libera acá.
[[nodiscard]] std::optional<std::string> ReadEnvironmentVariable(const char* name);
} // namespace Client::Core
