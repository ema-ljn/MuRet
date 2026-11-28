#pragma once

#include <cstddef>
#include <string>

// Por qué no se pudo leer muret-client.ini (ParseConfig, Config.h).
namespace Client::App
{
struct ConfigError
{
    // La línea del primer error, contando desde 1.
    std::size_t line = 0;
    // Para el usuario, sin la línea: quien lo imprime la pone adelante.
    std::string message;
};
} // namespace Client::App
