#pragma once

#include "Network/SocketApi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// Un listener de verdad en 127.0.0.1, con el puerto que elige el sistema, no bloqueante, que el
// test consulta en el mismo hilo que el cliente: sin hilos y sin red de afuera, así corre en CI en
// los dos sistemas (pregunta 4 del plan de la CL4). Usa las llamadas de Network/SocketApi.h, donde
// está el único #ifdef. Necesita un SocketApi::NetworkInit vivo, como todo socket.
//
// Atiende a un cliente por vez: Accept toma el próximo y suelta el anterior.
namespace Client::Tests
{
// Cuánto espera un test con sockets de verdad lo que el sistema hace en el fondo: un connect, un
// accept, unos bytes, un cierre. En Linux, en loopback, casi todo llega en la misma llamada; en
// Windows un rechazo tarda unos 2 s, por los reintentos (el plan, "Lo que más puede morder").
inline constexpr std::chrono::seconds LoopbackTimeout{5};

// Llama a `attempt` hasta que dé true o pasen LoopbackTimeout segundos reales, sin dormir entre
// una y otra (cede el hilo). true si llegó.
[[nodiscard]] bool RetryUntil(const std::function<bool()>& attempt);

class LoopbackServer
{
public:
    // Si no puede escuchar, el test falla (ADD_FAILURE) y Port() da 0.
    LoopbackServer();

    [[nodiscard]] std::uint16_t Port() const;
    // true si había un cliente esperando, que pasa a ser el de Read, Write y los cierres.
    [[nodiscard]] bool Accept();
    // Lo que haya llegado del cliente, sin esperar; vacío si no llegó nada o si cerró.
    [[nodiscard]] std::vector<std::byte> Read();
    // Si un Read vio que el cliente cerró (un FIN o un RST). Accept lo vuelve a false.
    [[nodiscard]] bool ClientClosed() const;
    // Lo manda entero, o el test falla: para los pocos bytes de un test, el sistema los toma todos.
    void Write(std::span<const std::byte> bytes);
    // close: el cliente recibe un FIN, y su recv da 0.
    void CloseClient();
    // SO_LINGER {1, 0} y close: un RST, como el kick de 0x4e8380; el recv del cliente da
    // ECONNRESET.
    void ResetClient();

private:
    Network::SocketApi::UniqueSocket m_listener;
    Network::SocketApi::UniqueSocket m_client;
    std::uint16_t m_port = 0;
    bool m_clientClosed = false;
};
} // namespace Client::Tests
