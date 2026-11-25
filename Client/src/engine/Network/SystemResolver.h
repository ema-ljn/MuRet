#pragma once

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/SocketError.h"

#include <string_view>

// El IResolver del sistema: una IPv4 en texto sale sin preguntar a nadie (inet_pton), y un nombre
// va al DNS con getaddrinfo y AF_INET, que espera lo que tarde, como el gethostbyname de 0x4e8570
// (pregunta 5 del plan de la CL4). Un nombre que no resuelve es ResolveFailed: el original sigue
// como si hubiera conectado (devuelve 2 y 0x4c1340 solo mira el 0).
//
// Necesita un SocketApi::NetworkInit vivo: en Windows, getaddrinfo es de Winsock.
namespace Client::Network
{
class SystemResolver final : public IResolver
{
public:
    // Vacío o con un byte 0 adentro es ResolveFailed sin preguntar: el sistema lee el texto hasta
    // el primer 0 y resolvería otro nombre.
    [[nodiscard]] Core::Expected<Ipv4Address, SocketError> Resolve(std::string_view host) override;
};
} // namespace Client::Network
