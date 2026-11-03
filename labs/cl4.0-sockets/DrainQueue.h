#pragma once

#include "LabSocket.h"

#include <array>
#include <cstddef>
#include <functional>
#include <span>

// Misión de la CL4.0 (docs/learning/ola-cl4.0-sockets-no-bloqueantes.md, actividad 8): lo que el
// socket no tomó, con un cursor, para mandarlo después sin repetir ni perder nada. Es lo que
// 0x4e87b0 quiere hacer con FD_WRITE y no hace (Q-C11, el jefe).
//
// El buffer es fijo, del tamaño del buffer de envío del original (+0x10 del objeto de red, con el
// largo en +0x2010): 0x2000 bytes. No crece: lo que no entra se rechaza, y el original, en ese
// caso, cierra la conexión.
namespace Labs
{
inline constexpr std::size_t DrainQueueCapacity = 0x2000;

// Un send: toma lo que pueda de los bytes y dice cuánto en IoResult::bytes, o "probá más tarde", o
// un error. LabSocket::Send lo cumple, y los tests le pasan uno que sigue un guion.
using SendFunction = std::function<IoResult(std::span<const std::byte>)>;

class DrainQueue
{
public:
    // Agrega `bytes` detrás de lo pendiente. false, sin agregar nada, si lo pendiente más lo nuevo
    // pasa de DrainQueueCapacity. Antes de copiar corre lo pendiente al principio: lo que ya salió
    // no ocupa lugar.
    [[nodiscard]] bool Push(std::span<const std::byte> bytes);
    // Manda desde el cursor mientras `send` tome algo. Done si quedó vacía; TryLater si el send
    // dijo "probá más tarde" o no tomó nada; y si falló, el estado del error. En los dos últimos
    // casos lo que falta queda, con el cursor donde estaba. Con la cola vacía no llama a `send`.
    [[nodiscard]] IoStatus Flush(const SendFunction& send);
    // Lo que falta mandar.
    [[nodiscard]] std::size_t Pending() const;

private:
    std::array<std::byte, DrainQueueCapacity> m_buffer{};
    // El cursor: lo que ya salió va de 0 a m_begin, y lo pendiente, de m_begin a m_end.
    std::size_t m_begin = 0;
    std::size_t m_end = 0;
};
} // namespace Labs
