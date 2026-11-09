#pragma once

#include <cstddef>

// Fuente síncrona del byte extra del envoltorio C3/C4 (03-red-y-cifrado § C3/C4).
// El owner presta una instancia durable: Connection no la copia, reemplaza ni reinicia al abrir.
namespace Client::Network
{
class IPacketPaddingSource
{
public:
    virtual ~IPacketPaddingSource() = default;
    [[nodiscard]] virtual std::byte NextPadding() = 0;
};
} // namespace Client::Network
