#pragma once

#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// El envío que no entró en el socket, con un cursor: lo que salió no se vuelve a mandar.
// Contrato: el plan de la CL4 (CL4.1), "Contrato", y Q-C11 en 07-quirks-y-bugs.md.
//
// El buffer de envío del original mide 0x2000 (+0x10, el largo en +0x2010, en 0x4e87b0) y su
// drenaje resta dos veces lo que salió: con varios envíos parciales seguidos repite el principio y
// pierde el final (Q-C11). Este lleva lo que salió en un cursor, en un std::array fijo del mismo
// tamaño: Push corre lo pendiente al principio cuando lo nuevo no entra detrás, así que el cursor
// no lo hace crecer, y lo que no entra en 0x2000 bytes es Full, como el original, que cierra.
//
// Quien la usa (la Connection, CL4.3) nunca manda directo con algo pendiente: lo nuevo va detrás
// (el envío en línea de 0x4b1664 sí, y desordena).
namespace Client::Network
{
inline constexpr std::size_t MaxPendingSend = 0x2000;

enum class SendQueueError : std::uint8_t
{
    Full,   // lo pendiente más lo nuevo pasa de MaxPendingSend: el original cierra
    Socket, // el socket falló: SocketError en LastSocketError()
};

class SendQueue
{
public:
    // Agrega `bytes` detrás de lo pendiente, o Full sin agregar nada.
    [[nodiscard]] std::optional<SendQueueError> Push(std::span<const std::byte> bytes);

    // Manda lo que el socket tome, desde el cursor, hasta que no tome más o no quede nada. Sin nada
    // pendiente no llama al socket. Con un error del socket, lo pendiente queda como estaba.
    [[nodiscard]] std::optional<SendQueueError> Flush(ISocket& socket);

    [[nodiscard]] std::size_t Pending() const;

    // El del último Socket; Closed si no hubo ninguno.
    [[nodiscard]] SocketError LastSocketError() const;

    // Tira lo pendiente: la conexión se cerró y lo que no salió no va a la próxima.
    void Clear();

private:
    // Corre lo pendiente al principio del buffer.
    void Compact();

    std::array<std::byte, MaxPendingSend> m_buffer{};
    std::size_t m_sent = 0; // el cursor: lo que ya salió de lo que hay
    std::size_t m_end = 0;  // hasta dónde hay
    SocketError m_lastSocketError = SocketError::Closed;
};
} // namespace Client::Network
