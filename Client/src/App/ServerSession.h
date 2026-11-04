#pragma once

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "Core/IClock.h"
#include "Core/IntervalTimer.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Dispatcher.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// La sesión con los servidores del juego: con el ConnectServer, hasta la dirección del GameServer
// (CL4.3); con el GameServer, hasta el login (CL4.4); y después, el keepalive (CL4.5). Desde la
// CL5.5 no elige ni manda la cuenta sola: espera a la UI (o al AutoLogin) en ChoosingServer y en
// WaitingCredentials. Desde la CL6.3, la lista de personajes y la entrada al mundo, que pide la
// escena de la selección. Contrato: el plan de la CL4, "Contrato" y el diagrama de estados de la
// CL4.3, paso 1, y "Contrato" de la CL4.4 y de la CL4.5; el de la CL5.5 en el plan de la CL5; el de
// la CL6.3 en el plan de la CL6; 04-protocolo.md, "El flujo de conexión" y "La selección de
// personaje (F3)".
//
// La consulta el bucle una vez por frame (Poll). Recorre los eventos de la Connection en orden, y
// despacha cada paquete en el Dispatcher del servidor con el que habla: el opcode 00 es el saludo
// en el ConnectServer y el chat en el GameServer. Cada estado espera un paquete; uno que llega
// fuera de turno se ignora con un aviso, y un cierre de la conexión es Failed. Lo mismo con una
// llamada de la UI fuera de turno: ChooseServer o SubmitCredentials en otro estado.
//
//   Idle o cualquiera --Start: abre el CS--> ConnectingConnectServer
//   ConnectingConnectServer --Opened: manda el A9--> WaitingConnectHello
//   WaitingConnectHello --00: manda F4 02--> WaitingServerList
//   WaitingServerList --F4 06: guarda la lista--> ChoosingServer
//   WaitingServerList --F4 06 vacía: cierra--> Failed (EmptyServerList)
//   ChoosingServer --ChooseServer: manda F4 03 con el código--> WaitingServerAddress
//   WaitingServerAddress --F4 03: cierra el CS y abre el GS con las claves--> ConnectingGameServer
//   WaitingServerAddress --F4 05: manda F4 02 (Q-C71)--> WaitingServerList
//   ConnectingGameServer --Opened--> WaitingGameHello
//   WaitingGameHello --F1 00 aceptado y con la versión--> WaitingCredentials
//   WaitingGameHello --F1 00 con otro resultado--> Failed (HelloRejected)
//   WaitingGameHello --F1 00 con otra versión--> Failed (VersionMismatch)
//   WaitingCredentials --SubmitCredentials: manda el login--> WaitingLoginResult
//   WaitingCredentials --SubmitCredentials con unas que no entran--> Failed (CredentialsTooLong)
//   WaitingLoginResult --F1 01 con 1: manda el primer keepalive--> LoggedIn
//   WaitingLoginResult --F1 01 con otro: guarda el resultado--> WaitingCredentials
//   LoggedIn --RequestCharacterList: manda F3 00--> WaitingCharacterList
//   WaitingCharacterList --F3 00: guarda la lista, aunque venga vacía--> ChoosingCharacter
//   ChoosingCharacter --EnterWorld con uno elegido: manda F3 03 con su nombre--> EnteringWorld
//   LoggedIn y los tres de después --cada KeepalivePeriod desde el primero: manda el keepalive-->
//     el mismo estado
//   con el GS --F1 02 con 0: cierra, sin aviso--> QuitOrdered (Q-C106)
//   cualquier Waiting* que espera un servidor --ReplyTimeout sin su respuesta: cierra--> Failed
//     (NoReply); ChoosingServer, WaitingCredentials y ChoosingCharacter esperan al jugador y no
//     tienen tope, y EnteringWorld tampoco: su respuesta es de la CL7
//   con el CS --Closed--> Failed (ConnectServerLost)
//   con el GS --Closed--> Failed (GameServerLost)
//
// El F1 02 con 0 es la orden del GS de cerrar el juego: el original hace WM_DESTROY (0x4c391b), sin
// aviso y en cualquier escena, y main.cpp cierra con QuitOrdered al terminar el frame. El GS la
// manda al final de su cuenta de 5 s (gObjCloseSet) y justo después cierra la conexión: ese cierre
// ya no es GameServerLost, porque la sesión cerró antes y deja el resto del lote, como el original,
// que al cerrar vacía su cola de paquetes (0x4e8380). QuitOrdered es el final: ni Start la saca. Con
// 1 o con 2 (la selección o la lista, Q-C79) se ignora con un aviso hasta la CL6 y la CL9.
//
// El keepalive corre en LoggedIn y en los tres estados de la selección, también minimizada, con el
// ritmo que empezó con el login, y se detiene al salir de ellos: la sesión no se reconecta sola (la
// escena de login muestra el aviso). El original manda desde el arranque
// si hay conexión al GS, aunque no haya login (pregunta 6 del plan de la CL4): acá tampoco sale
// mientras se espera la cuenta y la clave.
//
// Un login rechazado deja el GS abierto, como el original, que con el OK del aviso vuelve al
// estado 2 y abre otra vez la cuenta (0x643560): el próximo login sale por la misma conexión.
//
// Un C3/C4 del GS con el checksum malo se contesta con el F1 03, en cualquier estado, como el
// original. Uno que no mide bloques enteros, también: el original lee de más para el último
// bloque, y ese checksum casi siempre falla, así que es probable que conteste igual (03, "C3/C4").
// Los otros que no se descifran solo dejan el aviso de la conexión.
//
// Un handler que cambia de conexión (el F4 03) o que la cierra (una lista vacía, un F1 02 con 0, un
// Send que falla) deja el resto del lote sin recorrer: es de una conexión que ya no está, y lo que
// quedaba del CS no llega nunca al dispatcher del GS. Si la cerró el otro lado, lo que llegó antes
// del cierre se despacha, y después el Closed.
namespace Client::App
{
// 04 § El flujo de conexión: el timer 1000 de 20.000 ms del original, y 156 keepalives, uno cada
// 20 s, en el cable (02 § Una discrepancia entre documentos).
inline constexpr Core::Duration KeepalivePeriod = std::chrono::seconds{20};

// Lo que espera cada estado Waiting* su respuesta. No sale del original: el research no le vio un
// tope, y sin uno, un servidor que acepta y no contesta deja la sesión esperando para siempre, sin
// un error. Los Connecting* tienen el ConnectTimeout de la Connection, y LoggedIn no tiene tope:
// el GS puede no mandar nada por un rato.
inline constexpr Core::Duration ReplyTimeout = std::chrono::seconds{30};

// Las velocidades del keepalive: las del personaje antes de su ajuste, y los bits de
// personaje+0x3C que eligen el ajuste (KeepaliveSpeed, CL2.5). Un struct y no tres argumentos,
// para que las dos velocidades no se cambien de lugar sin que se note. Sin personaje, 0 y 0, sin
// ajuste, como el original (CL4.R, paso 6).
struct KeepaliveSpeeds
{
    std::uint16_t attackSpeed = 0;
    std::uint16_t magicSpeed = 0;
    std::uint8_t characterFlags = 0;
};

// Desde la CL5.5 la cuenta y la clave no están acá: llegan con SubmitCredentials, de la ventana de
// la cuenta o del AutoLogin.
struct SessionSettings
{
    ServerAddress connectServer; // de la config o de /u /p (CL3.4)
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

    // Conecta al ConnectServer de la config, desde cualquier estado menos QuitOrdered: lo que había
    // se cierra y se olvida (la falla, la lista, la elección, el GameServer y el resultado del
    // login). Con la orden de cerrar el juego, un aviso y nada: el juego se cierra en ese frame.
    void Start();

    // Una vez por frame, desde ILoopClient::Poll: primero lo que llegó, después el tope del
    // estado y por último el keepalive, si toca. Así lo que llegó durante un frame largo se lee
    // antes de mirar el tope.
    void Poll();

    [[nodiscard]] SessionState State() const;

    // Cuántas veces se llamó a Start: lo que elige o manda alguien de afuera (el AutoLogin) es de
    // una vuelta, y con un Start empieza otra.
    [[nodiscard]] std::uint32_t StartCount() const;

    // La lista del F4 06, en el orden en que llegó, desde que llega hasta el próximo Start o la
    // próxima lista. Vacía antes.
    [[nodiscard]] std::span<const Protocol::Connection::ServerEntry> ServerList() const;

    // En ChoosingServer, manda el F4 03 con `code` (0x645e10, el clic en un servidor de la lista).
    // El código no se mira contra la lista: lo da la ventana, que solo tiene los de la lista. En
    // otro estado, un aviso y nada.
    void ChooseServer(std::uint16_t code);

    // El código del último F4 03, desde ChooseServer hasta el próximo Start o un F4 05 (el servidor
    // ocupado, que vuelve a la lista).
    [[nodiscard]] std::optional<std::uint16_t> ChosenServer() const;

    // En WaitingCredentials, manda el login con `credentials` y el tick del reloj de ahora
    // (0x641bf0). Con unas que no entran, Failed (CredentialsTooLong). En otro estado, un aviso
    // sin ellas, y nada. No las guarda.
    void SubmitCredentials(const Credentials& credentials);

    // Por qué falló; std::nullopt si no está en Failed.
    [[nodiscard]] std::optional<SessionFailure> Failure() const;

    // La dirección que dio el F4 03, desde que llegó hasta el próximo Start.
    [[nodiscard]] std::optional<ServerAddress> GameServer() const;

    // El resultado del último F1 01, aceptado o no, desde que llegó hasta el próximo Start o el
    // próximo SubmitCredentials. Con WaitingCredentials, el rechazado: el aviso de la escena de
    // login (UI::NoticeOfLoginResult).
    [[nodiscard]] std::optional<std::uint8_t> LoginResult() const;

    // Las velocidades de los keepalives que siguen. Hasta que se llame, las de un KeepaliveSpeeds
    // vacío. Valen también después de un Start: son de quien tiene el personaje (CL6).
    void SetKeepaliveSpeeds(const KeepaliveSpeeds& speeds);

    // En LoggedIn, manda el F3 00 (0x634e0e, cuando 0x634c60 pasa a la escena 4) y espera la lista.
    // En otro estado, un aviso y nada.
    void RequestCharacterList();

    // La lista del último F3 00, desde que llega hasta el próximo Start. std::nullopt antes.
    [[nodiscard]] const std::optional<Protocol::Character::CharacterList>& Characters() const;

    // En ChoosingCharacter, elige al de `slot` si está en la lista; con std::nullopt o un lugar que
    // no está, a ninguno (el -1 de 0x6342c0). No manda nada. En otro estado, un aviso y nada.
    void ChooseCharacter(std::optional<std::uint8_t> slot);

    // El elegido, de la lista; nullptr si no hay. Vale hasta el próximo Start.
    [[nodiscard]] const Protocol::Character::CharacterEntry* ChosenCharacter() const;

    // En ChoosingCharacter con uno elegido, manda el F3 03 con su nombre (0x636290) y espera la
    // entrada. Sin elegido, o en otro estado, un aviso y nada.
    void EnterWorld();

private:
    void RegisterConnectServerHandlers();
    void RegisterGameServerHandlers();

    // Los eventos de la conexión, en orden, hasta que un handler cambie o cierre la conexión.
    void HandleEvents();

    void OnOpened();
    void OnPacket(const Network::Packet& packet);
    void OnUndecryptable(const Network::Undecryptable& undecryptable);
    void OnClosed(Network::CloseReason reason);

    void OnConnectHello(const Network::Packet& packet);
    void OnServerList(const Network::Packet& packet);
    void OnServerAddress(const Network::Packet& packet);
    void OnServerBusy();

    void OnGameHello(const Network::Packet& packet);
    void OnLoginResult(const Network::Packet& packet);
    void OnCharacterList(const Network::Packet& packet);
    void OnLogoutResult(const Network::Packet& packet);
    // El keepalive con el tick de `now` y las velocidades de SetKeepaliveSpeeds.
    void SendKeepalive(Core::Duration now);

    // Abre la conexión a `address`, que cierra la que hubiera, con `keys` o sin claves.
    void Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys);
    // Cierra la conexión, si no estaba cerrada, y pasa a Failed.
    void Fail(SessionFailure failure);
    // Pasa a `state` desde ahora, para el tope de los Waiting*; fuera de LoggedIn, sin keepalive.
    void SetState(SessionState state);

    // Si el estado es de los que hablan con el ConnectServer.
    [[nodiscard]] bool WithConnectServer() const;
    // "ConnectServer" o "GameServer", para el log, según con cuál habla el estado.
    [[nodiscard]] std::string_view ServerName() const;

    Network::Connection& m_connection;
    Core::IClock& m_clock;
    Core::Log& m_log;
    Network::ConnectionKeys m_keys;
    SessionSettings m_settings;
    std::uint32_t m_starts = 0;
    Protocol::Dispatcher m_connectServer;
    Protocol::Dispatcher m_gameServer;
    SessionState m_state = SessionState::Idle;
    Core::Duration m_stateSince{}; // desde cuándo está en m_state
    std::optional<SessionFailure> m_failure;
    std::vector<Protocol::Connection::ServerEntry> m_serverList;
    std::optional<std::uint16_t> m_chosenServer;
    std::optional<ServerAddress> m_gameServerAddress;
    std::optional<std::uint8_t> m_loginResult;
    std::optional<Protocol::Character::CharacterList> m_characters;
    std::optional<std::uint8_t> m_chosenSlot; // un lugar de m_characters
    // Cambia con cada Connect y cada Fail: Poll deja de recorrer el lote si cambió.
    std::uint32_t m_generation = 0;
    Core::IntervalTimer m_keepalive{KeepalivePeriod};
    KeepaliveSpeeds m_speeds;
};
} // namespace Client::App
