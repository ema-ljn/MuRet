#pragma once

#include "Core/Expected.h"
#include "Network/SocketError.h"

#include <array>
#include <cstdint>
#include <string_view>

// De un nombre o una IPv4 en texto a una IPv4. El del sistema (SystemResolver) espera mientras
// pregunta al DNS, como el gethostbyname de 0x4e8570 (pregunta 5 del plan de la CL4); el de los
// tests (FakeResolver) contesta lo que le digan, sin DNS.
namespace Client::Network
{
// Los cuatro bytes en el orden en que se escriben: 127.0.0.1 es {127, 0, 0, 1}.
using Ipv4Address = std::array<std::uint8_t, 4>;

class IResolver
{
public:
    virtual ~IResolver() = default;
    // La primera IPv4 de `host`, o ResolveFailed si no hay ninguna.
    [[nodiscard]] virtual Core::Expected<Ipv4Address, SocketError> Resolve(std::string_view host) = 0;

protected:
    // Como IObfuscation: se copia y se mueve solo desde una clase hija, para que asignar a través
    // de dos IResolver& no copie solo la base (slicing).
    IResolver() = default;
    IResolver(const IResolver&) = default;
    IResolver& operator=(const IResolver&) = default;
    IResolver(IResolver&&) = default;
    IResolver& operator=(IResolver&&) = default;
};
} // namespace Client::Network
