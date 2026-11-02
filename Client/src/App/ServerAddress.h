#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// La dirección del ConnectServer, de la config ([Server] Host y Port) o de la línea de comandos
// (/u<ip> /p<puerto>). Las dos la validan con las mismas funciones, así dicen lo mismo.
namespace Client::App
{
// El puerto compilado del ConnectServer, 0x6d5dc4 (02 § Del launcher al juego).
inline constexpr std::uint16_t DefaultConnectPort = 44405;
// Q-C37: el host del launcher llegaba a 255 bytes y se copiaba sin mirar el largo.
inline constexpr std::size_t MaxHostBytes = 255;

struct ServerAddress
{
    // Vacío si no se dio: el host se exige recién cuando arranca la red (CL4).
    std::string host;
    std::uint16_t port = DefaultConnectPort;
};

// Un puerto escrito en decimal, solo dígitos y de 1 a 65535. El error es "el puerto va de 1 a
// 65535" o "el puerto tiene que ser un número".
Core::Expected<std::uint16_t, std::string> ParsePort(std::string_view text);

// Un host de 1 a MaxHostBytes bytes y sin espacios. No resuelve nombres ni mira si es una IP.
Core::Expected<std::string, std::string> ParseHost(std::string_view text);
} // namespace Client::App
