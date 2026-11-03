#pragma once

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Protocol/Dispatcher.h"

#include <cstdint>
#include <optional>

// La sesión con los servidores del juego: con el ConnectServer, hasta la dirección del GameServer
// (CL4.3), y con el GameServer, hasta el login (CL4.4).
// Contrato: el plan de la CL4, "Contrato" y el diagrama de estados de la CL4.3, paso 1, y
// "Contrato" de la CL4.4; 04-protocolo.md, "El flujo de conexión".
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
//   WaitingServerAddress --F4 03: cierra el CS y abre el GS con las claves--> ConnectingGameServer
//   ConnectingGameServer --Opened--> WaitingGameHello
//   WaitingGameHello --F1 00 aceptado y con la versión: manda el login--> WaitingLoginResult
//   WaitingGameHello --F1 00 con otro resultado--> Failed (HelloRejected)
//   WaitingGameHello --F1 00 con otra versión--> Failed (VersionMismatch)
//   WaitingGameHello --F1 00 con unas credenciales que no entran--> Failed (CredentialsTooLong)
//   WaitingLoginResult --F1 01 con 1--> LoggedIn
//   WaitingLoginResult --F1 01 con otro--> Failed (LoginRejected)
//   con el CS --Closed--> Failed (ConnectServerLost)
//   con el GS --Closed--> Failed (GameServerLost)
//
// Un C3/C4 del GS con el checksum malo se contesta con el F1 03, en cualquier estado, como el
// original. Uno que no mide bloques enteros, también: el original lee de más para el último
// bloque, y ese checksum casi siempre falla, así que es probable que conteste igual (03, "C3/C4").
// Los otros que no se descifran solo dejan el aviso de la conexión.
//
// Un handler que cambia de conexión (el F4 03) o que la cierra (una lista vacía, un Send que
// falla) deja el resto del lote sin recorrer: es de una conexión que ya no está, y lo que quedaba
// del CS no llega nunca al dispatcher del GS. Si la cerró el otro lado, lo que llegó antes del
// cierre se despacha, y después el Closed.
namespace Client::App
{
struct SessionSettings
{
    ServerAddress connectServer; // de la config o de /u /p (CL3.4)
    Credentials credentials;     // para el login (CL4.4)
};

class ServerSession
{
public:
    // La conexión, el reloj y el log son de quien la arma y tienen que vivir más que ella. El reloj
    // da el tick del login; `keys` son las de la conexión con el GameServer (Enc1.dat y Dec2.dat):
    // el ConnectServer abre sin claves.
    ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log, Network::ConnectionKeys keys,
                  SessionSettings settings);

    // Los handlers guardan this: no se copia ni se mueve.
    ServerSession(const ServerSession&) = delete;
    ServerSession& operator=(const ServerSession&) = delete;
    ~ServerSession() = default;

    // Conecta al ConnectServer de la config, desde cualquier estado: lo que había se cierra y
    // se olvida (la falla, el GameServer y el resultado del login).
    void Start();

    // Una vez por frame, desde ILoopClient::Poll.
    void Poll();

    [[nodiscard]] SessionState State() const;

    // Por qué falló; std::nullopt si no está en Failed.
    [[nodiscard]] std::optional<SessionFailure> Failure() const;

    // La dirección que dio el F4 03, desde que llegó hasta el próximo Start.
    [[nodiscard]] std::optional<ServerAddress> GameServer() const;

    // El resultado del F1 01, aceptado o no, desde que llegó hasta el próximo Start. Qué dice cada
    // uno es de la escena de login (CL5.5).
    [[nodiscard]] std::optional<std::uint8_t> LoginResult() const;

private:
    void RegisterConnectServerHandlers();
    void RegisterGameServerHandlers();

    void OnOpened();
    void OnPacket(const Network::Packet& packet);
    void OnUndecryptable(const Network::Undecryptable& undecryptable);
    void OnClosed(Network::CloseReason reason);

    void OnConnectHello(const Network::Packet& packet);
    void OnServerList(const Network::Packet& packet);
    void OnServerAddress(const Network::Packet& packet);

    void OnGameHello(const Network::Packet& packet);
    void OnLoginResult(const Network::Packet& packet);
    void SendLogin();

    // Abre la conexión a `address`, que cierra la que hubiera, con `keys` o sin claves.
    void Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys);
    // Cierra la conexión, si no estaba cerrada, y pasa a Failed.
    void Fail(SessionFailure failure);

    // Si el estado es de los que hablan con el ConnectServer.
    [[nodiscard]] bool WithConnectServer() const;

    Network::Connection& m_connection;
    Core::IClock& m_clock;
    Core::Log& m_log;
    Network::ConnectionKeys m_keys;
    SessionSettings m_settings;
    Protocol::Dispatcher m_connectServer;
    Protocol::Dispatcher m_gameServer;
    SessionState m_state = SessionState::Idle;
    std::optional<SessionFailure> m_failure;
    std::optional<ServerAddress> m_gameServerAddress;
    std::optional<std::uint8_t> m_loginResult;
    // Cambia con cada Connect y cada Fail: Poll deja de recorrer el lote si cambió.
    std::uint32_t m_generation = 0;
};
} // namespace Client::App
