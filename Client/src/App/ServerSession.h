#pragma once

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Protocol/Dispatcher.h"

#include <cstdint>
#include <optional>

// La sesión con los servidores del juego: con el ConnectServer, hasta la dirección del GameServer
// (CL4.3), y con el GameServer, hasta el login (CL4.4).
// Contrato: el plan de la CL4 (CL4.3), "Contrato" y el diagrama de estados del paso 1;
// 04-protocolo.md, "El flujo de conexión".
//
// La consulta el bucle una vez por frame (Poll). Recorre los eventos de la Connection en orden, y
// despacha cada paquete en el Dispatcher del servidor con el que habla: el opcode 00 es el saludo
// en el ConnectServer y el chat en el GameServer. Cada estado espera un paquete; uno que llega
// fuera de turno se ignora con un aviso, y un cierre de la conexión es Failed.
//
//   Idle o cualquiera --Start: abre el CS--> ConnectingConnectServer
//   ConnectingConnectServer --Opened: manda el A9--> WaitingConnectHello
//   WaitingConnectHello --00: manda F4 02--> WaitingServerList
//   WaitingServerList --F4 06: manda F4 03 con el primero--> WaitingServerAddress
//   WaitingServerList --F4 06 vacía: cierra--> Failed (EmptyServerList)
//   WaitingServerAddress --F4 03: cierra el CS y abre el GS--> ConnectingGameServer
//   con el CS --Closed--> Failed (ConnectServerLost)
//   ConnectingGameServer --Closed--> Failed (GameServerLost)
//
// Un handler que cambia de conexión (el F4 03) o que la cierra (una lista vacía, un Send que
// falla) deja el resto del lote sin recorrer: es de una conexión que ya no está, y lo que quedaba
// del CS no llega nunca al dispatcher del GS. Si la cerró el otro lado, lo que llegó antes del
// cierre se despacha, y después el Closed.
namespace Client::App
{
enum class SessionState : std::uint8_t
{
    Idle,                    // sin Start
    ConnectingConnectServer, // Start abrió el CS
    WaitingConnectHello,     // el A9 salió; se espera C1 04 00 01
    WaitingServerList,       // F4 02 salió
    WaitingServerAddress,    // F4 03 salió
    ConnectingGameServer,    // el CS se cerró y se abrió el GS; lo que sigue es de la CL4.4
    Failed,
};

enum class SessionFailure : std::uint8_t
{
    ConnectServerLost, // la conexión con el CS se cerró: el motivo va al log y queda en la conexión
    EmptyServerList,   // el CS no tiene ningún servidor
    GameServerLost,    // la conexión con el GS se cerró
};

struct SessionSettings
{
    ServerAddress connectServer; // de la config o de /u /p (CL3.4)
    Credentials credentials;     // para el login (CL4.4)
};

class ServerSession
{
public:
    // La conexión y el log son de quien la arma y tienen que vivir más que ella.
    ServerSession(Network::Connection& connection, Core::Log& log, SessionSettings settings);

    // Los handlers guardan this: no se copia ni se mueve.
    ServerSession(const ServerSession&) = delete;
    ServerSession& operator=(const ServerSession&) = delete;
    ~ServerSession() = default;

    // Conecta al ConnectServer de la config, desde cualquier estado: lo que había se cierra y
    // se olvida (la falla y el GameServer).
    void Start();

    // Una vez por frame, desde ILoopClient::Poll.
    void Poll();

    [[nodiscard]] SessionState State() const;

    // Por qué falló; std::nullopt si no está en Failed.
    [[nodiscard]] std::optional<SessionFailure> Failure() const;

    // La dirección que dio el F4 03, desde que llegó hasta el próximo Start.
    [[nodiscard]] std::optional<ServerAddress> GameServer() const;

private:
    void RegisterConnectServerHandlers();

    void OnOpened();
    void OnPacket(const Network::Packet& packet);
    void OnClosed(Network::CloseReason reason);

    void OnConnectHello(const Network::Packet& packet);
    void OnServerList(const Network::Packet& packet);
    void OnServerAddress(const Network::Packet& packet);

    // Abre la conexión a `address`, que cierra la que hubiera.
    void Connect(const ServerAddress& address);
    // Cierra la conexión, si no estaba cerrada, y pasa a Failed.
    void Fail(SessionFailure failure);

    // Si el estado es de los que hablan con el ConnectServer.
    [[nodiscard]] bool WithConnectServer() const;

    Network::Connection& m_connection;
    Core::Log& m_log;
    SessionSettings m_settings;
    Protocol::Dispatcher m_connectServer;
    Protocol::Dispatcher m_gameServer; // vacío hasta la CL4.4
    SessionState m_state = SessionState::Idle;
    std::optional<SessionFailure> m_failure;
    std::optional<ServerAddress> m_gameServerAddress;
    // Cambia con cada Connect y cada Fail: Poll deja de recorrer el lote si cambió.
    std::uint32_t m_generation = 0;
};
} // namespace Client::App
