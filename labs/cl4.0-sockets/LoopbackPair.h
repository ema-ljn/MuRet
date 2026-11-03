#pragma once

#include "LabSocket.h"

#include <cstdint>
#include <optional>

// Misión de la CL4.0 (docs/learning/ola-cl4.0-sockets-no-bloqueantes.md, actividad 5): un listener
// en 127.0.0.1, con el puerto que elija el sistema, y un cliente que le conecta sin bloquear. Los
// dos lados no bloqueantes y en el mismo hilo: el test pregunta, una vez por vuelta, y nadie
// espera. Es el "una vez por frame" de D-C5 sin el frame.
namespace Labs
{
class LoopbackPair
{
public:
    // Abre el listener (bind a 127.0.0.1:0, listen, no bloqueante) y empieza el connect del
    // cliente, ya no bloqueante. std::nullopt si el sistema no dio un socket o el bind o el listen
    // fallaron.
    [[nodiscard]] static std::optional<LoopbackPair> Open();

    // Lo que dio el connect al empezar: InProgress, o Done si el sistema ya lo terminó.
    [[nodiscard]] const IoResult& ConnectStarted() const;
    // El puerto del listener.
    [[nodiscard]] std::uint16_t Port() const;
    // Acepta al cliente si ya llegó, y lo pone no bloqueante. true si hay uno aceptado, también si
    // ya estaba de antes.
    [[nodiscard]] bool Accept();

    [[nodiscard]] LabSocket& Client();
    // El lado del servidor: vacío hasta que Accept dé true.
    [[nodiscard]] LabSocket& Server();

private:
    LoopbackPair() = default;

    LabSocket m_listener;
    LabSocket m_client;
    LabSocket m_server;
    IoResult m_connectStarted;
    std::uint16_t m_port = 0;
};
} // namespace Labs
