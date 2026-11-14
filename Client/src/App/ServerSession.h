#pragma once

#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/IntervalTimer.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Dispatcher.h"
#include "Protocol/FloorItems.h"
#include "Protocol/Inventory.h"
#include "Protocol/Movement.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// La sesión con los servidores del juego: con el ConnectServer, hasta la dirección del GameServer
// (CL4.3); con el GameServer, hasta el login (CL4.4), y el keepalive mientras esté conectado
// (CL4.5). Desde la CL5.5 no elige ni manda la cuenta sola: espera a la UI (o al AutoLogin) en ChoosingServer y en
// WaitingCredentials. Desde la CL6.3, la lista de personajes y la entrada al mundo, que pide la
// escena de la selección. Contrato: el plan de la CL4, "Contrato" y el diagrama de estados de la
// CL4.3, paso 1, y "Contrato" de la CL4.4 y de la CL4.5; el de la CL5.5 en el plan de la CL5; el de
// la CL6.3 en el plan de la CL6; 04-protocolo.md, "El flujo de conexión" y "La selección de
// personaje (F3)".
//
// La consulta el bucle una vez por frame (Poll). Recorre los eventos de la Connection en orden, y
// despacha cada paquete en el Dispatcher del servidor con el que habla: el opcode 00 es el saludo
// en el ConnectServer y el chat en el GameServer. Cada estado espera un paquete; uno que llega
// fuera de turno se ignora con un aviso. Una llamada de la UI fuera de turno (ChooseServer o
// SubmitCredentials en otro estado) también se ignora con un aviso. Un cierre de la conexión es
// Failed. Hay recepciones que no dependen del estado, como el F1 02 con 0 (abajo) y las de mundo:
// F3/03 y F3/10 se entregan al sink con el GS en cualquier estado, incluso WaitingGameHello,
// antes del saludo y del login, si llegaron cifradas (C3/C4) y se leen. Ilegibles se descartan con un
// aviso; en claro, el F3 03 y el F3 10 le avisan al GameServer con el F1 03 00 F3, y el F3 10 antes le
// entrega al sink su inicialización común (ClearInventoryReceived). El sink aplica cada recepción antes
// del siguiente evento, con el contexto vivo; también recibe en orden las de personajes. La sesión no
// retiene payloads para otro frame.
// El clima 0F legible también va al sink sin guarda de estado ni de cifrado; no escribe la cuenta
// actual ni cambia el estado de sesión. Un 0F corto o con otro contenedor se descarta con aviso.
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
//   WaitingLoginResult --F1 01 con 1: manda un keepalive inmediato--> LoggedIn
//   WaitingLoginResult --F1 01 con otro: guarda el resultado--> WaitingCredentials
//   LoggedIn --RequestCharacterList: manda F3 00--> WaitingCharacterList
//   WaitingCharacterList --F3 00: guarda la lista, aunque venga vacía--> ChoosingCharacter
//   ChoosingCharacter --EnterWorld con uno elegido: manda F3 03 con su nombre--> EnteringWorld
//   ChoosingCharacter --CreateCharacter: manda F3 01 con el nombre y la clase--> WaitingCreateResult
//   WaitingCreateResult --F3 01 con 1: suma al nuevo, sin equipo--> ChoosingCharacter
//   WaitingCreateResult --F3 01 con 0 o 2: guarda el resultado--> ChoosingCharacter
//   WaitingCreateResult --F3 01 con otro: un aviso (el original se queda, Q-C114)--> el mismo
//   ChoosingCharacter --DeleteCharacter con uno elegido: manda F3 02 con su nombre y el código-->
//     WaitingDeleteResult
//   WaitingDeleteResult --F3 02 con 1: saca al que se mandó (Q-C104)--> ChoosingCharacter
//   WaitingDeleteResult --F3 02 con otro: guarda el resultado--> ChoosingCharacter
//   con el GS abierto --cada KeepalivePeriod del timer global: manda el keepalive-->
//     el mismo estado
//   con el GS --F3 03 o F3 10 cifrado y legible: aplica en el sink--> el mismo estado
//   con el GS --F3 10 en claro: aplica en el sink el vaciado y manda F1 03--> el mismo estado
//   aplicando un F3 03 --PublishWorldEntry, en el store del 0x3d (0x4c4854)--> InWorld
//   un error del sink --cierra y corta el lote--> Failed (ApplicationFailed), también después de
//     publicar InWorld
//   con el GS --F1 02 con 0: cierra, sin aviso--> QuitOrdered (Q-C106)
//   con el GS --F1 02 con 1 cifrado: aplica en el sink la vuelta a la selección y manda F3 00 y dos
//     C1 03 31--> WaitingCharacterList (Q-C211 a Q-C215)
//   con el GS --F1 02 con 1 en claro: manda F1 03 00 F1--> el mismo estado (Q-C211)
//   cualquier Waiting* que espera un servidor --ReplyTimeout sin su respuesta: cierra--> Failed
//     (NoReply); ChoosingServer, WaitingCredentials y ChoosingCharacter esperan al jugador y no
//     tienen tope; EnteringWorld tampoco, como el mosaico del original mientras no llega la
//     respuesta (06 § La entrada al mundo, paso 4), e InWorld no espera ninguna respuesta
//   con el CS --Closed--> Failed (ConnectServerLost)
//   con el GS --Closed--> Failed (GameServerLost)
//
// El F1 02 con 0 es la orden del GS de cerrar el juego: el original hace WM_DESTROY (0x4c391b), sin
// aviso y en cualquier escena, y main.cpp cierra con QuitOrdered al terminar el frame. El GS la
// manda al final de su cuenta de 5 s (gObjCloseSet) y justo después cierra la conexión: ese cierre
// ya no es GameServerLost, porque la sesión cerró antes y deja el resto del lote, como el original,
// que al cerrar vacía su cola de paquetes (0x4e8380). QuitOrdered es el final: ni Start la saca. Con
// 2 (la lista, Q-C79) se ignora con un aviso hasta la CL9.
//
// El F1 02 con 1 vuelve a la selección, también en cualquier estado con el GS y sin mirar la escena
// (0x4c2710). Cifrado, el sink limpia el juego y deja la orden de la escena 4 (Scenes::ApplyReturnToSelection),
// la sesión olvida la lista y la elección, pide la lista nueva (el F3 00 de 0x4c3653) y manda el C1 03 31
// de 0x419900 y el de 0x4499b0, en ese orden, como el original; la selección espera la lista
// (WaitingCharacterList, con su ReplyTimeout) y no la vuelve a pedir. Sin cifrar no cambia de escena:
// solo el F1 03 00 F1 (06, "La vuelta a la selección").
//
// El timer del keepalive empieza al construir la sesión y conserva su fase al cambiar de estado,
// aceptar/rechazar el login y reconectar, salvo el aborto de aplicación descrito abajo. En cada
// vencimiento manda solo si el GS está abierto, aunque se espere el saludo, la cuenta o el resultado,
// también minimizada. El login aceptado manda
// además uno inmediato sin reiniciar el timer, como el original (04 § El flujo de conexión). La
// sesión no se reconecta sola: la escena de login muestra el aviso de cierre.
// ApplicationFailed suspende también el consumo del timer hasta el primer Poll posterior a Start.
// Ese Poll retoma IntervalTimer::Due: si perdió un periodo o más, el próximo vence desde ahora;
// no se promete la misma fase que habría tenido al seguir consumiendo durante el fallo.
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
    // El ejecutable que suma la respuesta al 03 del GS: el nombre que 0x4ad270 saca de la línea de
    // comandos (Protocol::Connection::MainCheckFileName), relativo a la carpeta actual como el
    // CreateFileA de 0x4aec13. Vacío, o si no se abre, la suma va en 0, como en 0x4aec28.
    std::filesystem::path mainCheckFile{};
};

class ISessionEventSink;

class ServerSession
{
public:
    // La conexión, el reloj, el log y el sink son de quien la arma y viven más que ella. El reloj
    // da el tick del login; `keys` son las de la conexión con el GameServer (Enc1.dat y Dec2.dat):
    // el ConnectServer abre sin claves. El timer global de 20 s empieza al construir la sesión.
    ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log, Network::ConnectionKeys keys,
                  SessionSettings settings, ISessionEventSink& eventSink);

    // Los handlers guardan this: no se copia ni se mueve.
    ServerSession(const ServerSession&) = delete;
    ServerSession& operator=(const ServerSession&) = delete;
    ~ServerSession() = default;

    // Conecta al ConnectServer de la config, desde cualquier estado menos QuitOrdered: lo que había
    // se cierra y se olvida (la falla, la lista, la elección, el GameServer y el resultado del
    // login). Con la orden de cerrar el juego, un aviso y nada: el juego se cierra en ese frame.
    void Start();

    // Una vez por frame, desde ILoopClient::Poll: despacha lo que llegó, consume el vencimiento
    // del timer aun sin GS, comprueba el tope del estado y manda el keepalive si toca y el GS sigue
    // abierto. Así lo que llegó durante un frame largo se lee antes de mirar el tope. El primer error
    // de aplicación se devuelve también en los Poll siguientes, hasta Start, sin timer ni keepalive.
    Core::Expected<void, std::string> Poll();

    [[nodiscard]] SessionState State() const;

    // El store del 0x3d de la respuesta del F3 03 (0x4c4854, fila 9 de la tabla de 0x4c3940 en 06):
    // pasa a InWorld. Lo llama el sink desde el ApplyWorld de un F3 03, después de cargar el mapa y
    // de armar al héroe (filas 1 a 8), y lo que sigue de la respuesta corre ya en InWorld. No mira
    // el estado anterior, como 0x4c3940: una respuesta que se vuelve a aplicar lo vuelve a publicar.
    // Fuera de esa aplicación (otro momento o el F3 10, que no escribe el 0x3d), un aviso y nada.
    void PublishWorldEntry();

    // Si la sesión está en InWorld: el F3 03 aplicado publicó el 0x3d.
    [[nodiscard]] bool JoinedWorld() const;

    // DAT_057678bc, la clave del jugador: la que trae el F1 00 aceptado (0x4c1ec0, antes de mirar la
    // versión) hasta el próximo. 0 antes, como la .bss. La respuesta del F3 03 la pone en el héroe.
    [[nodiscard]] std::uint16_t PlayerIndex() const;

    // 04 § Caminar y pegar y § Los cambios de mapa. El caller decide cuándo corresponde;
    // la sesión sólo exige un GameServer abierto, nunca manda estos paquetes al ConnectServer.
    [[nodiscard]] Core::Expected<void, std::string> SendWalk(std::span<const Protocol::Movement::Cell> path,
                                                             Protocol::Movement::Cell target, float angle);
    [[nodiscard]] Core::Expected<void, std::string> SendFacing(std::uint8_t direction);
    // CL9 hito 3: el D7 de pegar (0x5650c0, Protocol::Combat::WriteAttack), XOR32, que FoxAnti saca como A4.
    [[nodiscard]] Core::Expected<void, std::string> SendAttack(std::uint16_t key, std::uint8_t action,
                                                               std::uint8_t direction);
    // CL9 hito 3: el DF de la celda del elfo que camina antes de pegar (0x59cf88, Protocol::Combat::WriteElfCell).
    [[nodiscard]] Core::Expected<void, std::string> SendElfCell(std::uint8_t x, std::uint8_t y);
    // El C1 18 de sentarse (0x4f2416..0x4f2bea) y del emote (0x576a60 y los casos de 0x576eb0), con
    // Protocol::Movement::WriteAction: como el original con el socket en −1, sin un GameServer abierto un aviso y nada.
    void SendAction(std::uint8_t direction, std::uint8_t action);
    // 0x61c210: el C1 1C F3 30 con las opciones (Protocol::OptionData::WriteOptionData), en claro. Lo piden el menú
    // de Esc (0x624036, 0x6247ee y 0x624eec) y la entrada a los mapas 30 y 31.
    [[nodiscard]] Core::Expected<void, std::string> SendOptionData(const Protocol::OptionData::OptionBody& body);
    void RequestGate(std::uint8_t gate);
    void ConfirmTeleport();
    void RequestCrywolfState();
    // El chat (C1 00) y el susurro (C1 02) que arma la entrada del chat (Protocol::Chat), como los demás de mundo: sin
    // un GameServer abierto, un aviso y nada. Lo que lleva no va al log.
    void SendChat(Protocol::Outgoing packet);
    // El B9 02 de 0x42a760 al cargar el 30 (Protocol::World::WriteCastleSiegeState).
    void RequestCastleSiegeState();
    // El 24 del ítem del cursor (Protocol::Inventory::WriteItemMove), en C3. Los dos rand() del envío los saca la
    // escena antes.
    [[nodiscard]] Core::Expected<void, std::string> SendItemMove(const Protocol::Inventory::ItemMove& move);
    // CL9 hito 5: el C1 05 22 (0x5650c0), el C1 06 23 (0x5dfe27) y el C1 06 26 (0x59ff10 y 0x5c4110), en C3. Los dos
    // rand() del envío los saca la escena antes.
    [[nodiscard]] Core::Expected<void, std::string> SendPickUp(std::uint16_t index);
    [[nodiscard]] Core::Expected<void, std::string> SendDrop(const Protocol::FloorItems::Drop& drop);
    [[nodiscard]] Core::Expected<void, std::string> SendUseItem(const Protocol::FloorItems::UseItem& use);
    // El C1 03 42 de 0x6599a0(4), al abrir la ventana de la party (Protocol::Party::WritePartyListRequest), en claro.
    void RequestPartyList();
    // Las misiones y los NPC (CL9 hito 2), por el GS en el mundo. Los rand() del relleno y del serial de los C3 los
    // saca la escena antes. El C3 A0 de 0x63693a y 0x5676a6 (Protocol::Quest::WriteQuestListRequest).
    void RequestQuestList();
    // El C3 A2 de 0x419bd0 y 0x41a9e0 (Protocol::Quest::WriteSetQuest).
    void SendSetQuest(std::uint8_t quest, std::uint8_t state);
    // El C3 30 de la llegada a un NPC (0x5650c0, Protocol::Quest::WriteTalk).
    void SendTalk(std::int16_t key);
    // El C1 03 31 en claro de 0x419900, al cerrar la ventana de las misiones (Protocol::Connection::WriteWindowClose).
    void SendWindowClose();
    // El C0 y los C1..C4 que mandan la ventana de amigos y sus cajas (Protocol::Friends), como los demás de mundo.
    void SendFriendPacket(Protocol::Outgoing packet);
    // 0x4c609f: el 00 con la escena 2 (el login) pide la lista de servidores, C1 04 F4 02, por el socket que esté
    // abierto, sin mirar cuál es. En el CS es el saludo (OnConnectHello); acá, el que llega por el GS (Q-C511).
    void ResendServerListRequest();
    // 0x641203 y 0x63f9a0, al abrir y al cerrar los créditos: C1 04 F4 06 en claro por el socket que esté
    // abierto (0x6c82a0, el del ConnectServer mientras se ve la lista), sin mirar cuál es. Cerrado, nada:
    // el original no manda con el socket en −1.
    void SendCreditsNotice();

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
    // sin ellas, y nada. Si el GS ya cerró y falta despachar Closed, tampoco cambia el estado ni
    // el resultado anterior. No las guarda.
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

    // La vista del último F3 00, desde que llega hasta Start. Un slot repetido conserva su última
    // entrada, también durante el callback y después de un fallo de aplicación. El sink recibe
    // cada construcción cruda del paquete, en orden, sin deduplicar el evento.
    // std::nullopt antes de recibir la lista.
    [[nodiscard]] const std::optional<Protocol::Character::CharacterList>& Characters() const;

    // En ChoosingCharacter, elige al de `slot` si está en la lista; con std::nullopt o un lugar que
    // no está, a ninguno (el -1 de 0x6342c0). No manda nada. En otro estado, un aviso y nada.
    void ChooseCharacter(std::optional<std::uint8_t> slot);

    // El elegido, de la lista; nullptr si no hay. Vale hasta el próximo Start.
    [[nodiscard]] const Protocol::Character::CharacterEntry* ChosenCharacter() const;

    // En ChoosingCharacter con uno elegido, manda el F3 03 con su nombre (0x636290) y espera la
    // entrada. Sin elegido, o en otro estado, un aviso y nada.
    void EnterWorld();

    // En ChoosingCharacter, manda el F3 01 con `name` y la clase `characterClass` (de 0 a 4, la de la
    // vista previa: 0x63d990) y espera la respuesta. El nombre no va al log. Uno de más de 10 bytes o
    // con un cero, o en otro estado, un aviso y nada: los chequeos del nombre de antes (el largo y los
    // filtros) son de la ventana de crear.
    void CreateCharacter(std::string_view name, std::uint8_t characterClass);

    // El pkt[4] de la última respuesta del F3 01 (Protocol::Character::CharacterCreated, o el de un
    // aviso), desde que llega hasta el próximo CreateCharacter o Start. std::nullopt antes.
    [[nodiscard]] std::optional<std::uint8_t> CreateResult() const;

    // En ChoosingCharacter con uno elegido, manda el F3 02 con su nombre y los bytes del campo de la
    // ventana de borrar (0x643740), y espera la respuesta. El código no va al log. Sin elegido, o en
    // otro estado, un aviso y nada.
    void DeleteCharacter(std::span<const std::byte, Protocol::Character::DeleteCodeSize> code);

    // En ChoosingCharacter o InWorld, manda el pedido de salida F1 02 con `kind` (0x647f60, el C3 que arma
    // la ventana de Esc) y sigue en el mismo estado: lo que cambia es la respuesta (OnLogoutResult). Los
    // rand() de la CRT de 0x647f60 los saca quien lo pide. En otro estado, un aviso y nada.
    void RequestLogout(std::uint8_t kind);

    // El pkt[4] de la última respuesta del F3 02 (Protocol::Character::CharacterDeleted, o el de un
    // aviso), desde que llega hasta el próximo DeleteCharacter o Start. std::nullopt antes.
    [[nodiscard]] std::optional<std::uint8_t> DeleteResult() const;

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
    void OnCreateResult(const Network::Packet& packet);
    void OnDeleteResult(const Network::Packet& packet);
    void OnWorldEntryReply(const Network::Packet& packet);
    void OnInventoryReply(const Network::Packet& packet);
    void OnWeather(const Network::Packet& packet);
    void OnWalk(const Network::Packet& packet);
    void OnDamage(const Network::Packet& packet);
    // CL9 hito 3: el 0x16 (0x4cf200), el 0x9C (0x4cfce0) y el 0x17 (0x4d0a30).
    void OnKillExperience(const Network::Packet& packet);
    void OnExtendedKillExperience(const Network::Packet& packet);
    void OnDeath(const Network::Packet& packet);
    void OnAction(const Network::Packet& packet);
    // Los otros jugadores en el viewport (CL9 hito 1): 0x12, 0x14, 0x45, 0x25 y 0xDF.
    void OnViewportPlayers(const Network::Packet& packet);
    void OnViewportLeave(const Network::Packet& packet);
    void OnTransformed(const Network::Packet& packet);
    void OnEquipmentShown(const Network::Packet& packet);
    void OnViewportPosition(const Network::Packet& packet);
    void OnViewportMonsters(const Network::Packet& packet);
    void OnSummons(const Network::Packet& packet);
    // El 1A (0x4cedd0): la skill que pega a varios.
    void OnMagicAttackCount(const Network::Packet& packet);
    void OnViewportObjects(const Network::Packet& packet);
    void OnTeleport(const Network::Packet& packet);
    void OnRegen(const Network::Packet& packet);
    void OnChat(const Network::Packet& packet);
    void OnKeyChat(const Network::Packet& packet);
    void OnWhisper(const Network::Packet& packet);
    void OnWhisperResult(const Network::Packet& packet);
    void OnNotice(const Network::Packet& packet);
    void OnOptionData(const Network::Packet& packet);
    void OnItemMove(const Network::Packet& packet);
    void OnLifeRefill(const Network::Packet& packet);
    void OnManaRefill(const Network::Packet& packet);
    void OnItemDelete(const Network::Packet& packet);
    void OnDurability(const Network::Packet& packet);
    void OnEquipmentChange(const Network::Packet& packet);
    void OnItemPut(const Network::Packet& packet);
    void OnFloorItemsCreate(const Network::Packet& packet);
    void OnFloorItemsDelete(const Network::Packet& packet);
    void OnPickUpResult(const Network::Packet& packet);
    void OnDropResult(const Network::Packet& packet);
    void OnStatFruit(const Network::Packet& packet);
    void OnLogoutResult(const Network::Packet& packet);
    void OnMainCheck(const Network::Packet& packet);
    // Las misiones (CL9 hito 2): 9F, A0, A1, A2 y A3, sin guarda de escena ni del cifrado.
    void OnQuestFlag(const Network::Packet& packet);
    void OnQuestList(const Network::Packet& packet);
    void OnQuestState(const Network::Packet& packet);
    void OnSetQuestResult(const Network::Packet& packet);
    void OnQuestResult(const Network::Packet& packet);
    // CL9 hito 9: los amigos (C0..C4, 0x4e5a4e..0x4e5aae), sin guarda de escena ni del flag de cifrado.
    void OnFriendList(const Network::Packet& packet);
    void OnFriendAddResult(const Network::Packet& packet);
    void OnFriendRequest(const Network::Packet& packet);
    void OnFriendDeleteResult(const Network::Packet& packet);
    void OnFriendState(const Network::Packet& packet);
    // El F1 02 con 1, con `encrypted` en el flag del paquete.
    void ReturnToSelection(bool encrypted);
    // El keepalive con el tick de `now` y las velocidades de SetKeepaliveSpeeds.
    void SendKeepalive(Core::Duration now);

    // Abre la conexión a `address`, que cierra la que hubiera, con `keys` o sin claves.
    void Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys);
    // Cierra la conexión, si no estaba cerrada, y pasa a Failed.
    void Fail(SessionFailure failure);
    // Conserva el primer diagnóstico y corta el lote con la generación existente.
    bool CheckApplication(Core::Expected<void, std::string> applied);
    // No encola mensajes de juego en el socket del ConnectServer ni en una conexión cerrada.
    [[nodiscard]] Core::Expected<void, std::string> SendWorldPacket(Protocol::Outgoing outgoing);
    // Pasa a `state` desde ahora, para el tope de los Waiting*; no reinicia el timer global.
    void SetState(SessionState state);

    // Si el estado es de los que hablan con el ConnectServer.
    [[nodiscard]] bool WithConnectServer() const;
    // "ConnectServer" o "GameServer", para el log, según con cuál habla el estado.
    [[nodiscard]] std::string_view ServerName() const;

    Network::Connection& m_connection;
    Core::IClock& m_clock;
    Core::Log& m_log;
    ISessionEventSink& m_eventSink;
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
    std::uint16_t m_playerIndex = 0;
    std::optional<Protocol::Character::CharacterList> m_characters;
    std::optional<std::string> m_applicationFailure;
    // Solo durante el ApplyWorld de un F3 03: ahí vale PublishWorldEntry.
    bool m_applyingWorldEntry = false;
    std::optional<std::uint8_t> m_chosenSlot; // un lugar de m_characters
    std::optional<std::uint8_t> m_createResult;
    std::optional<std::uint8_t> m_deleteResult;
    std::optional<std::uint8_t> m_deletingSlot; // el lugar del F3 02 que salió
    // Cambia con cada Connect y cada Fail: Poll deja de recorrer el lote si cambió.
    std::uint32_t m_generation = 0;
    Core::IntervalTimer m_keepalive{KeepalivePeriod};
    KeepaliveSpeeds m_speeds;
};
} // namespace Client::App
