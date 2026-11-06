#include "App/ServerSession.h"

#include "App/CharacterEvent.h"
#include "App/Credentials.h"
#include "App/FaithfulPacer.h"
#include "App/ServerAddress.h"
#include "App/SessionState.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Character/CharacterCreate.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterName.h"
#include "Protocol/Character/CharacterSelect.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Hello.h"
#include "Protocol/Connection/Keepalive.h"
#include "Protocol/Connection/Login.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Connection/ServerList.h"
#include "Protocol/Dispatcher.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Client::App
{
namespace
{
// Lo que dice el aviso de un paquete que el estado no espera, o que no se puede leer.
constexpr std::string_view OutOfTurn = "llegó fuera de turno";
constexpr std::string_view Unreadable = "no se puede leer";
// Lo que dice el aviso de una llamada de la UI que el estado no espera.
constexpr std::string_view NotAwaited = "la sesión no la espera";
constexpr std::string_view NotAwaitedPlural = "la sesión no las espera";
constexpr std::string_view NotAwaitedMasculine = "la sesión no lo espera";

// Un visitor de std::visit con un operator() por tipo: si Network::Event suma un tipo, Poll no
// compila hasta que la sesión diga qué hace con él.
template <typename... Handlers> struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};
template <typename... Handlers> Overloaded(Handlers...) -> Overloaded<Handlers...>;

// La versión del saludo como texto para el log: viene del servidor, y un byte que no se ve (un \0,
// un salto de línea) sale como '?', así la línea sigue siendo una.
std::string VersionText(const std::array<char, Protocol::VersionSize>& version)
{
    std::string text(version.begin(), version.end());
    std::ranges::replace_if(text, [](char value) { return value < ' ' || value > '~'; }, '?');
    return text;
}

// Lo que espera cada estado Waiting*, para el log de ReplyTimeout; vacío en los que no esperan una
// respuesta de un servidor. Los que esperan al jugador no tienen tope.
std::string_view Awaited(SessionState state)
{
    switch (state)
    {
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingGameHello:
        return "su saludo";
    case SessionState::WaitingServerList:
        return "la lista de servidores";
    case SessionState::WaitingServerAddress:
        return "la dirección del GameServer";
    case SessionState::WaitingLoginResult:
        return "el resultado del login";
    case SessionState::WaitingCharacterList:
        return "la lista de personajes";
    case SessionState::WaitingCreateResult:
        return "el resultado de crear un personaje";
    case SessionState::WaitingDeleteResult:
        return "el resultado de borrar un personaje";
    case SessionState::Idle:
    case SessionState::ConnectingConnectServer:
    case SessionState::ChoosingServer:
    case SessionState::ConnectingGameServer:
    case SessionState::WaitingCredentials:
    case SessionState::LoggedIn:
    case SessionState::ChoosingCharacter:
    case SessionState::EnteringWorld:
    case SessionState::QuitOrdered:
    case SessionState::Failed:
        break;
    }
    return {};
}

// Los errores que se contestan con el F1 03: el checksum malo, como el original, y lo que no mide
// bloques enteros, como contesta probablemente el original (03, "C3/C4").
bool DeservesTheReport(const Network::Undecryptable& undecryptable)
{
    return undecryptable.error == Network::Crypto::CryptoError::ChecksumMismatch ||
           undecryptable.error == Network::Crypto::CryptoError::NotWholeBlocks;
}

// 0x4c20a0 construye en orden sobre pool[slot]: la última entrada reemplaza la anterior.
// La vista UI es única por slot; el evento conserva aparte todas las construcciones recibidas.
void KeepLastCharacterPerSlot(Protocol::Character::CharacterList& list)
{
    std::vector<Protocol::Character::CharacterEntry> unique;
    unique.reserve(list.characters.size());
    for (auto entry = list.characters.rbegin(); entry != list.characters.rend(); ++entry)
    {
        if (std::ranges::find(unique, entry->slot, &Protocol::Character::CharacterEntry::slot) == unique.end())
        {
            unique.push_back(*entry);
        }
    }
    std::ranges::reverse(unique);
    list.characters = std::move(unique);
}
} // namespace

ServerSession::ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log,
                             Network::ConnectionKeys keys, SessionSettings settings)
    : m_connection(connection), m_clock(clock), m_log(log), m_keys(keys), m_settings(std::move(settings)),
      m_connectServer(log), m_gameServer(log)
{
    RegisterConnectServerHandlers();
    RegisterGameServerHandlers();
    // El timer global no depende de abrir una conexión ni de autenticar (04 § El flujo de conexión).
    m_keepalive.Start(m_clock.Now());
}

void ServerSession::RegisterConnectServerHandlers()
{
    using Protocol::Connection::ConnectHelloOpcode;
    using Protocol::Connection::ServerBusySubcode;
    using Protocol::Connection::ServerListOpcode;
    using Protocol::Connection::ServerListSubcode;
    using Protocol::Connection::ServerSelectSubcode;
    // Un Dispatcher recién hecho no tiene nada: registrar no puede fallar.
    [[maybe_unused]] const bool registered =
        m_connectServer.On(ConnectHelloOpcode, [this](const Network::Packet& packet) { OnConnectHello(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerListSubcode,
                           [this](const Network::Packet& packet) { OnServerList(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerSelectSubcode,
                           [this](const Network::Packet& packet) { OnServerAddress(packet); }) &&
        m_connectServer.On(ServerListOpcode, ServerBusySubcode, [this](const Network::Packet&) { OnServerBusy(); });
    assert(registered && "los handlers del ConnectServer se registran una vez");
}

void ServerSession::RegisterGameServerHandlers()
{
    using Protocol::Character::CharacterOpcode;
    using Protocol::Character::CreateSubcode;
    using Protocol::Character::DeleteSubcode;
    using Protocol::Character::ListSubcode;
    using Protocol::Connection::HelloSubcode;
    using Protocol::Connection::JoinOpcode;
    using Protocol::Connection::LoginSubcode;
    using Protocol::Connection::LogoutSubcode;
    [[maybe_unused]] const bool registered =
        m_gameServer.On(JoinOpcode, HelloSubcode, [this](const Network::Packet& packet) { OnGameHello(packet); }) &&
        m_gameServer.On(JoinOpcode, LoginSubcode, [this](const Network::Packet& packet) { OnLoginResult(packet); }) &&
        m_gameServer.On(CharacterOpcode, ListSubcode,
                        [this](const Network::Packet& packet) { OnCharacterList(packet); }) &&
        m_gameServer.On(CharacterOpcode, CreateSubcode,
                        [this](const Network::Packet& packet) { OnCreateResult(packet); }) &&
        m_gameServer.On(CharacterOpcode, DeleteSubcode,
                        [this](const Network::Packet& packet) { OnDeleteResult(packet); }) &&
        m_gameServer.On(JoinOpcode, LogoutSubcode, [this](const Network::Packet& packet) { OnLogoutResult(packet); });
    assert(registered && "los handlers del GameServer se registran una vez");
}

void ServerSession::Start()
{
    if (m_state == SessionState::QuitOrdered)
    {
        m_log.Warning("no se vuelve a conectar: el GameServer ordenó cerrar el juego");
        return;
    }
    ++m_starts;
    m_failure.reset();
    m_serverList.clear();
    m_chosenServer.reset();
    m_gameServerAddress.reset();
    m_loginResult.reset();
    m_characters.reset();
    m_characterEvents.clear();
    m_createResult.reset();
    m_deleteResult.reset();
    m_deletingSlot.reset();
    m_chosenSlot.reset();
    m_log.Info("se conecta al ConnectServer");
    Connect(m_settings.connectServer, std::nullopt);
    SetState(SessionState::ConnectingConnectServer);
}

void ServerSession::Poll()
{
    HandleEvents();
    const Core::Duration now = m_clock.Now();
    // También consume los vencimientos sin GS: no quedan pendientes para la próxima conexión.
    const bool keepaliveDue = m_keepalive.Due(now);
    const std::string_view awaited = Awaited(m_state);
    if (!awaited.empty() && now - m_stateSince >= ReplyTimeout)
    {
        m_log.Error("el {} no contestó en {} s: se esperaba {}", ServerName(),
                    std::chrono::duration_cast<std::chrono::seconds>(ReplyTimeout).count(), awaited);
        Fail(SessionFailure::NoReply);
        return;
    }
    if (keepaliveDue && m_connection.State() == Network::LinkState::Open && !WithConnectServer())
    {
        SendKeepalive(now);
    }
}

// Lo que queda del lote después de un Connect o un Fail es de la conexión de antes, y lo que queda
// después de que un Send de un handler cerró la conexión, de una que ya no está: ese cierre sale en
// el Poll siguiente (Connection.h). Si la cerró el Poll mismo (el otro lado, un corte perdido), el
// Closed es lo último del lote y lo que llegó antes se despacha.
void ServerSession::HandleEvents()
{
    const std::uint32_t generation = m_generation;
    const std::vector<Network::Event> events = m_connection.Poll();
    const bool closedByThePoll = m_connection.State() == Network::LinkState::Closed;
    for (const Network::Event& event : events)
    {
        std::visit(
            Overloaded{
                [this](const Network::Opened&) { OnOpened(); },
                [this](const Network::Packet& packet) { OnPacket(packet); },
                [this](const Network::Undecryptable& undecryptable) { OnUndecryptable(undecryptable); },
                [this](const Network::Closed& closed) { OnClosed(closed.reason); },
            },
            event);
        const bool closedByAHandler = !closedByThePoll && m_connection.State() == Network::LinkState::Closed;
        if (m_generation != generation || closedByAHandler)
        {
            return;
        }
    }
}

SessionState ServerSession::State() const
{
    return m_state;
}

std::uint32_t ServerSession::StartCount() const
{
    return m_starts;
}

std::span<const Protocol::Connection::ServerEntry> ServerSession::ServerList() const
{
    return m_serverList;
}

// 0x645e10 manda el F4 03 con el código del botón y cierra la lista; el log no dice cuál.
void ServerSession::ChooseServer(std::uint16_t code)
{
    if (m_state != SessionState::ChoosingServer)
    {
        m_log.Warning("se ignora la elección de un servidor: {}", NotAwaited);
        return;
    }
    m_log.Info("se elige un servidor: se pide su dirección");
    m_chosenServer = code;
    Protocol::Outgoing choice = Protocol::Connection::WriteServerSelect(code);
    m_connection.Send(std::move(choice.packet), choice.delivery);
    SetState(SessionState::WaitingServerAddress);
}

std::optional<std::uint16_t> ServerSession::ChosenServer() const
{
    return m_chosenServer;
}

// El tick es el de GetTickCount en el original (0x641bf0): el del reloj del bucle en este momento.
// Ni la cuenta ni la clave van al log, tampoco cuando no entran.
void ServerSession::SubmitCredentials(const Credentials& credentials)
{
    if (m_state != SessionState::WaitingCredentials)
    {
        m_log.Warning("se ignoran la cuenta y la clave: {}", NotAwaitedPlural);
        return;
    }
    // Un envío periódico puede haber cerrado el GS en el Poll de este mismo frame.
    // Su Closed se despacha en el siguiente: hasta entonces se conservan estado y resultado.
    if (m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("se ignoran la cuenta y la clave: el GameServer ya no está conectado");
        return;
    }
    const Protocol::Connection::LoginRequest request{credentials.account, credentials.password,
                                                     TickCountMs(m_clock.Now())};
    auto login = Protocol::Connection::WriteLoginRequest(request);
    if (!login.HasValue())
    {
        m_log.Error("no se puede escribir el login: la cuenta o la clave pasan de {} bytes",
                    Protocol::Connection::CredentialSize);
        Fail(SessionFailure::CredentialsTooLong);
        return;
    }
    m_log.Info("se manda el login");
    m_loginResult.reset();
    Protocol::Outgoing outgoing = std::move(login).Value();
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    SetState(SessionState::WaitingLoginResult);
}

std::optional<SessionFailure> ServerSession::Failure() const
{
    return m_failure;
}

std::optional<ServerAddress> ServerSession::GameServer() const
{
    return m_gameServerAddress;
}

std::optional<std::uint8_t> ServerSession::LoginResult() const
{
    return m_loginResult;
}

void ServerSession::SetKeepaliveSpeeds(const KeepaliveSpeeds& speeds)
{
    m_speeds = speeds;
}

// 0x634c60 pasa a la escena 4 y manda el F3 00 en el mismo paso (0x634dce, 0x634e0e): quien llama
// es la escena de la selección, al entrar.
void ServerSession::RequestCharacterList()
{
    if (m_state != SessionState::LoggedIn)
    {
        m_log.Warning("se ignora el pedido de la lista de personajes: {}", NotAwaitedMasculine);
        return;
    }
    m_log.Info("se pide la lista de personajes");
    Protocol::Outgoing request = Protocol::Character::WriteCharacterListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingCharacterList);
}

const std::optional<Protocol::Character::CharacterList>& ServerSession::Characters() const
{
    return m_characters;
}

std::vector<CharacterEvent> ServerSession::TakeCharacterEvents()
{
    return std::exchange(m_characterEvents, {});
}

void ServerSession::ChooseCharacter(std::optional<std::uint8_t> slot)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora la elección de un personaje: {}", NotAwaited);
        return;
    }
    m_chosenSlot = slot;
    if (ChosenCharacter() == nullptr)
    {
        m_chosenSlot.reset();
    }
}

const Protocol::Character::CharacterEntry* ServerSession::ChosenCharacter() const
{
    if (!m_characters || !m_chosenSlot)
    {
        return nullptr;
    }
    const auto& characters = m_characters->characters;
    const auto chosen = std::ranges::find(characters, *m_chosenSlot, &Protocol::Character::CharacterEntry::slot);
    return chosen == characters.end() ? nullptr : &*chosen;
}

// 0x636290 manda el F3 03 en el primer paso de la escena 5 y loguea el lugar y el nombre; acá, el
// lugar: el nombre es del jugador.
void ServerSession::EnterWorld()
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora la entrada al mundo: {}", NotAwaited);
        return;
    }
    const Protocol::Character::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se entra al mundo: no hay un personaje elegido");
        return;
    }
    m_log.Info("se entra al mundo con el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing select = Protocol::Character::WriteCharacterSelect(chosen->name);
    m_connection.Send(std::move(select.packet), select.delivery);
    SetState(SessionState::EnteringWorld);
}

// 0x63d990 manda el F3 01 después de sus chequeos del nombre, que son de la ventana: acá solo se mira
// que entre en el paquete. El nombre es del jugador y no va al log.
void ServerSession::CreateCharacter(std::string_view name, std::uint8_t characterClass)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de crear un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const std::optional<std::array<std::byte, Protocol::Character::NameSize>> bytes =
        Protocol::Character::NameFromText(name);
    if (!bytes)
    {
        m_log.Warning("no se crea el personaje: el nombre no entra en 10 bytes");
        return;
    }
    m_log.Info("se pide crear un personaje de la clase {}", characterClass);
    Protocol::Outgoing create = Protocol::Character::WriteCharacterCreate(*bytes, characterClass);
    m_connection.Send(std::move(create.packet), create.delivery);
    m_createResult.reset();
    SetState(SessionState::WaitingCreateResult);
}

std::optional<std::uint8_t> ServerSession::CreateResult() const
{
    return m_createResult;
}

// 0x643740 manda el F3 02 con el nombre del elegido y el campo de la ventana. El código es como la
// clave: no va al log.
void ServerSession::DeleteCharacter(std::span<const std::byte, Protocol::Character::DeleteCodeSize> code)
{
    if (m_state != SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de borrar un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const Protocol::Character::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se borra un personaje: no hay uno elegido");
        return;
    }
    m_log.Info("se pide borrar el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing request = Protocol::Character::WriteCharacterDelete(chosen->name, code);
    m_connection.Send(std::move(request.packet), request.delivery);
    m_deletingSlot = chosen->slot;
    m_deleteResult.reset();
    SetState(SessionState::WaitingDeleteResult);
}

std::optional<std::uint8_t> ServerSession::DeleteResult() const
{
    return m_deleteResult;
}

// Al CS, lo primero es el A9, como lo manda 0x415be0 apenas la escena de login conecta (Q-C16). El
// GS saluda primero: al abrirlo no sale nada.
void ServerSession::OnOpened()
{
    if (m_state == SessionState::ConnectingConnectServer)
    {
        m_log.Info("conectó con el ConnectServer");
        Protocol::Outgoing request = Protocol::Pet::WritePetInfoRequest(Protocol::Pet::LoginScenePetInfoRequest);
        m_connection.Send(std::move(request.packet), request.delivery);
        SetState(SessionState::WaitingConnectHello);
        return;
    }
    m_log.Info("conectó con el GameServer: se espera su saludo");
    SetState(SessionState::WaitingGameHello);
}

void ServerSession::OnPacket(const Network::Packet& packet)
{
    Protocol::Dispatcher& dispatcher = WithConnectServer() ? m_connectServer : m_gameServer;
    if (dispatcher.Dispatch(packet) == Protocol::DispatchResult::Malformed)
    {
        m_log.Warning("se descarta un paquete mal formado del {}", ServerName());
    }
}

// El aviso ya lo dejó la conexión. Sin claves (el CS) no hay error, y no se contesta.
void ServerSession::OnUndecryptable(const Network::Undecryptable& undecryptable)
{
    if (!DeservesTheReport(undecryptable))
    {
        return;
    }
    m_log.Info("un C3/C4 del GameServer no se descifró: se le avisa al GameServer (F1 03)");
    Protocol::Outgoing report = Protocol::Connection::WriteDecryptErrorReport();
    m_connection.Send(std::move(report.packet), report.delivery);
}

void ServerSession::OnClosed(Network::CloseReason reason)
{
    m_log.Error("se perdió la conexión con el {}: {}", ServerName(), Network::CloseReasonText(reason));
    Fail(WithConnectServer() ? SessionFailure::ConnectServerLost : SessionFailure::GameServerLost);
}

// 0x4e33e4 pide la lista con cada saludo: en 8 de 64 conexiones sale dos veces (04 § El flujo de
// conexión). Acá, una vez.
void ServerSession::OnConnectHello([[maybe_unused]] const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingConnectHello)
    {
        m_log.Warning("se ignora el saludo del ConnectServer (00): {}", OutOfTurn);
        return;
    }
    m_log.Info("el ConnectServer saludó: se pide la lista de servidores");
    Protocol::Outgoing request = Protocol::Connection::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingServerList);
}

// La lista se guarda en el orden en que llega, y la elección es de la UI (CL5.5) o del AutoLogin,
// que elige el primero (pregunta 9 del plan de la CL4). 0x4c1860 la reparte en grupos para la
// ventana: eso es de UI::GroupServers.
void ServerSession::OnServerList(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerList)
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", OutOfTurn);
        return;
    }
    const auto list = Protocol::Connection::ReadServerList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", Unreadable);
        return;
    }
    const std::vector<Protocol::Connection::ServerEntry>& entries = list.Value();
    if (entries.empty())
    {
        m_log.Error("la lista de servidores está vacía: no hay a qué GameServer conectarse");
        Fail(SessionFailure::EmptyServerList);
        return;
    }
    m_log.Info("la lista trae {} servidores: se espera la elección", entries.size());
    m_serverList = entries;
    SetState(SessionState::ChoosingServer);
}

// El original loguea la dirección, cierra el CS (0x4e8380) y conecta al GS (0x4c1340). Acá el log
// no la dice: es del servidor, como el host de la config.
void ServerSession::OnServerAddress(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", OutOfTurn);
        return;
    }
    const auto address = Protocol::Connection::ReadServerAddress(packet);
    if (!address.HasValue())
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", Unreadable);
        return;
    }
    m_log.Info("el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese");
    m_gameServerAddress = ServerAddress{address.Value().host, address.Value().port};
    Connect(*m_gameServerAddress, m_keys);
    SetState(SessionState::ConnectingGameServer);
}

// El F4 05 contesta la elección de un servidor ocupado. Como el original (0x4e2b10 llama a
// 0x4c1bf0, que manda C1 04 F4 02), la sesión vuelve a pedir la lista al mismo CS, para que el
// jugador elija otro (Q-C71). El byte que sigue al subcódigo, el servidor, no se lee: el original
// tampoco lo mira.
void ServerSession::OnServerBusy()
{
    if (m_state != SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora el aviso de servidor ocupado (F4 05): {}", OutOfTurn);
        return;
    }
    m_log.Warning("el servidor elegido está ocupado: se pide la lista otra vez");
    m_chosenServer.reset();
    Protocol::Outgoing request = Protocol::Connection::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(SessionState::WaitingServerList);
}

// 0x4c1e20 muestra "Connectting error" con otro resultado y "Version dismatch" con otra versión,
// aunque el resultado tampoco sea 1. Acá el resultado se mira primero: un saludo rechazado no
// llega a comparar la versión.
void ServerSession::OnGameHello(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingGameHello)
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", OutOfTurn);
        return;
    }
    const auto hello = Protocol::Connection::ReadServerHello(packet);
    if (!hello.HasValue())
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", Unreadable);
        return;
    }
    if (hello.Value().result != Protocol::Connection::HelloAccepted)
    {
        m_log.Error("el GameServer rechazó la conexión (resultado {})", hello.Value().result);
        Fail(SessionFailure::HelloRejected);
        return;
    }
    const std::string_view version(hello.Value().version.data(), hello.Value().version.size());
    if (version != Protocol::ClientVersion)
    {
        m_log.Error("el GameServer tiene la versión {} y este cliente la {}: no se manda el login",
                    VersionText(hello.Value().version), Protocol::ClientVersion);
        Fail(SessionFailure::VersionMismatch);
        return;
    }
    // 0x4c1e20 abre la ventana de la cuenta con el GS ya conectado (DAT_057678c0 en 2).
    m_log.Info("el GameServer saludó: se espera la cuenta y la clave");
    SetState(SessionState::WaitingCredentials);
}

void ServerSession::OnLoginResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingLoginResult)
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", OutOfTurn);
        return;
    }
    const auto result = Protocol::Connection::ReadLoginResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", Unreadable);
        return;
    }
    m_loginResult = result.Value();
    if (result.Value() != Protocol::Connection::LoginAccepted)
    {
        // El GS queda abierto: el aviso de la escena, y otra vez la cuenta (0x643560).
        m_log.Warning("el GameServer rechazó el login (resultado {}): se espera otra vez la cuenta y la clave",
                      result.Value());
        SetState(SessionState::WaitingCredentials);
        return;
    }
    m_log.Info("el GameServer aceptó el login");
    SetState(SessionState::LoggedIn);
    // 0x4ad100 manda uno inmediato, sin reiniciar el timer 1000 (04 § El flujo de conexión).
    SendKeepalive(m_clock.Now());
}

// La lista se guarda en el orden en que llega, aunque venga vacía: el original pasa igual al estado
// 0x33 (0x4c20a0). Elegir es del jugador.
void ServerSession::OnCharacterList(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingCharacterList)
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", OutOfTurn);
        return;
    }
    auto list = Protocol::Character::ReadCharacterList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", Unreadable);
        return;
    }
    m_characters = std::move(list).Value();
    m_characterEvents.emplace_back(CharacterListReceived{*m_characters});
    KeepLastCharacterPerSlot(*m_characters);
    m_chosenSlot.reset();
    m_log.Info("la lista trae {} personajes: se espera la elección", m_characters->characters.size());
    SetState(SessionState::ChoosingCharacter);
}

// 0x4c2290: con 1, el nuevo va a su lugar con su nombre, su nivel y su clase, sin equipo (0x517270,
// Q-C103), y pisa al que estuviera ahí; con 0 o 2, un aviso que vuelve a abrir la ventana. Con otro,
// el original no hace nada y se queda esperando con el aviso 0x5d abierto (Q-C114): acá, también, y
// el tope de la espera cierra la sesión. Un lugar de más de 4 no tiene posición en el original
// (0x4c2290 deja la de la pila): no se suma.
void ServerSession::OnCreateResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingCreateResult)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::Character::ReadCharacterCreateResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", Unreadable);
        return;
    }
    const Protocol::Character::CharacterCreateResult& created = read.Value();
    if (created.result != Protocol::Character::CharacterCreated &&
        created.result != Protocol::Character::CreateRefused &&
        created.result != Protocol::Character::CreateRefusedOther)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): el resultado {} no lo atiende el original",
                      created.result);
        return;
    }
    m_createResult = created.result;
    if (created.result == Protocol::Character::CharacterCreated)
    {
        if (created.slot > Protocol::Character::MaxCharacterSlot || !m_characters)
        {
            m_log.Warning("el personaje creado no se suma: el lugar {} no está en la selección", created.slot);
        }
        else
        {
            Protocol::Character::CharacterEntry entry;
            entry.slot = created.slot;
            entry.name = created.name;
            entry.level = created.level;
            entry.classByte = created.classByte;
            entry.withoutEquipment = true;
            entry.guildStatus = Protocol::Character::NoGuildStatus;
            std::vector<Protocol::Character::CharacterEntry>& characters = m_characters->characters;
            const auto same = std::ranges::find(characters, entry.slot, &Protocol::Character::CharacterEntry::slot);
            if (same != characters.end())
            {
                *same = entry;
            }
            else
            {
                characters.push_back(entry);
            }
            m_characterEvents.emplace_back(CharacterCreated{entry});
            m_log.Info("se creó el personaje del lugar {}", entry.slot);
        }
    }
    else
    {
        m_log.Info("el GameServer no creó el personaje: {}", created.result);
    }
    SetState(SessionState::ChoosingCharacter);
}

// 0x4e2b10 lee la respuesta en línea y solo abre su aviso; el 0x60 (con 1) borra al elegido cuando
// llega (Q-C104). Acá se saca al que se mandó, y queda sin elegido.
void ServerSession::OnDeleteResult(const Network::Packet& packet)
{
    if (m_state != SessionState::WaitingDeleteResult)
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::Character::ReadCharacterDeleteResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", Unreadable);
        return;
    }
    m_deleteResult = read.Value();
    if (*m_deleteResult == Protocol::Character::CharacterDeleted && m_characters && m_deletingSlot)
    {
        std::erase_if(m_characters->characters, [this](const Protocol::Character::CharacterEntry& entry)
                      { return entry.slot == *m_deletingSlot; });
        m_characterEvents.emplace_back(CharacterDeleted{*m_deletingSlot});
        m_chosenSlot.reset();
        m_log.Info("se borró el personaje del lugar {}", *m_deletingSlot);
    }
    else
    {
        m_log.Info("el GameServer no borró el personaje: {}", *m_deleteResult);
    }
    m_deletingSlot.reset();
    SetState(SessionState::ChoosingCharacter);
}

// 0x4c2710 no mira el estado ni la escena para el 0: SendMessageA(WM_DESTROY) en 0x4c391b, y el
// WM_DESTROY cierra el socket con un RST y vacía la cola de paquetes (0x4e8380), y hace
// PostQuitMessage. Acá se acepta en cualquier estado con el GS, como el original, aunque el GS
// solo la manda con el login aceptado (su cuenta corre con Connected >= 2). La conexión se cierra
// sin el Closed, y lo que quede del lote (otro paquete, o el cierre del GS si llegó en la misma
// lectura) no se recorre (Q-C106).
void ServerSession::OnLogoutResult(const Network::Packet& packet)
{
    const auto result = Protocol::Connection::ReadLogoutResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02): {}", Unreadable);
        return;
    }
    if (result.Value() != Protocol::Connection::LogoutQuitsTheGame)
    {
        // 1 es la selección y 2 la lista, que cambian de escena (Q-C79): la CL6 y la CL9. Con otro,
        // el original no hace nada.
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02 con {}): MuRet solo sigue la de "
                      "cerrar el juego, con 0",
                      result.Value());
        return;
    }
    m_log.Info("el GameServer ordenó cerrar el juego (F1 02 con 0): se cierra la conexión y el juego, sin aviso");
    m_connection.Close();
    ++m_generation;
    SetState(SessionState::QuitOrdered);
}

// El tick es el de GetTickCount en el original (0x4bee60), y las velocidades van con su ajuste.
void ServerSession::SendKeepalive(Core::Duration now)
{
    using Protocol::Connection::KeepaliveSpeed;
    const Protocol::Connection::Keepalive keepalive{TickCountMs(now),
                                                    KeepaliveSpeed(m_speeds.attackSpeed, m_speeds.characterFlags),
                                                    KeepaliveSpeed(m_speeds.magicSpeed, m_speeds.characterFlags)};
    Protocol::Outgoing outgoing = Protocol::Connection::WriteKeepalive(keepalive);
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    if (m_connection.State() == Network::LinkState::Open)
    {
        // La Connection pudo encolarlo: no afirma que el GS lo haya recibido.
        m_log.Info("se encoló el keepalive para el GameServer");
    }
}

// El CS va sin claves y el GS con las suyas: cada Open vuelve los seriales a 0.
void ServerSession::Connect(const ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys)
{
    m_connection.Open(address.host, address.port, keys);
    ++m_generation;
}

void ServerSession::Fail(SessionFailure failure)
{
    m_connection.Close();
    ++m_generation;
    m_failure = failure;
    SetState(SessionState::Failed);
}

void ServerSession::SetState(SessionState state)
{
    m_state = state;
    m_stateSince = m_clock.Now();
}

std::string_view ServerSession::ServerName() const
{
    return WithConnectServer() ? "ConnectServer" : "GameServer";
}

bool ServerSession::WithConnectServer() const
{
    switch (m_state)
    {
    case SessionState::ConnectingConnectServer:
    case SessionState::WaitingConnectHello:
    case SessionState::WaitingServerList:
    case SessionState::ChoosingServer:
    case SessionState::WaitingServerAddress:
        return true;
    case SessionState::Idle:
    case SessionState::ConnectingGameServer:
    case SessionState::WaitingGameHello:
    case SessionState::WaitingCredentials:
    case SessionState::WaitingLoginResult:
    case SessionState::LoggedIn:
    case SessionState::WaitingCharacterList:
    case SessionState::ChoosingCharacter:
    case SessionState::WaitingCreateResult:
    case SessionState::WaitingDeleteResult:
    case SessionState::EnteringWorld:
    case SessionState::QuitOrdered:
    case SessionState::Failed:
        break;
    }
    return false;
}
} // namespace Client::App
