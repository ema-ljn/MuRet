#include "Session/ServerSession.h"

#include "Session/CharacterEvent.h"
#include "Session/Credentials.h"
#include "Kernel/FaithfulPacer.h"
#include "App/IChatRoomLinks.h"
#include "Session/ISessionEventSink.h"
#include "Session/ServerAddress.h"
#include "Session/SessionState.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Network/CloseReason.h"
#include "Network/Connection.h"
#include "Crypto/CryptoError.h"
#include "Network/LinkState.h"
#include "Network/Packet.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/FrontEnd/CharacterCreate.h"
#include "Protocol/FrontEnd/CharacterDelete.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/FrontEnd/CharacterSelect.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Session/Hello.h"
#include "Protocol/Session/Keepalive.h"
#include "Protocol/Session/Login.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Session/MainCheck.h"
#include "Protocol/Session/MapServerMove.h"
#include "Protocol/Session/ServerList.h"
#include "Session/Dispatcher.h"
#include "Session/OpcodeTable.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Network/PacketFields.h"
#include "Protocol/Party/Party.h"
#include "Protocol/Pets/PetInfo.h"
#include "Protocol/Quests/Quest.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Movement/Movement.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
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
std::string VersionText(const std::array<char, Protocol::Session::VersionSize>& version)
{
    std::string text(version.begin(), version.end());
    std::ranges::replace_if(text, [](char value) { return value < ' ' || value > '~'; }, '?');
    return text;
}

// Lo que espera cada estado Waiting*, para el log de ReplyTimeout; vacío en los que no esperan una
// respuesta de un servidor. Los que esperan al jugador no tienen tope.
std::string_view Awaited(Session::SessionState state)
{
    switch (state)
    {
    case Session::SessionState::WaitingConnectHello:
    case Session::SessionState::WaitingGameHello:
        return "su saludo";
    case Session::SessionState::WaitingServerList:
        return "la lista de servidores";
    case Session::SessionState::WaitingServerAddress:
        return "la dirección del GameServer";
    case Session::SessionState::WaitingLoginResult:
        return "el resultado del login";
    case Session::SessionState::WaitingCharacterList:
        return "la lista de personajes";
    case Session::SessionState::WaitingCreateResult:
        return "el resultado de crear un personaje";
    case Session::SessionState::WaitingDeleteResult:
        return "el resultado de borrar un personaje";
    case Session::SessionState::Idle:
    case Session::SessionState::ConnectingConnectServer:
    case Session::SessionState::ChoosingServer:
    case Session::SessionState::ConnectingGameServer:
    case Session::SessionState::WaitingCredentials:
    case Session::SessionState::LoggedIn:
    case Session::SessionState::ChoosingCharacter:
    case Session::SessionState::EnteringWorld:
    case Session::SessionState::InWorld:
    case Session::SessionState::QuitOrdered:
    case Session::SessionState::Failed:
        break;
    }
    return {};
}

// Los errores que se contestan con el F1 03: el checksum malo, como el original, y lo que no mide
// bloques enteros, como contesta probablemente el original (03, "C3/C4").
bool DeservesTheReport(const Network::Undecryptable& undecryptable)
{
    return undecryptable.error == Crypto::CryptoError::ChecksumMismatch ||
           undecryptable.error == Crypto::CryptoError::NotWholeBlocks;
}

// 0x4c20a0 construye en orden sobre pool[slot]: la última entrada reemplaza la anterior.
// La vista UI es única por slot; el evento conserva aparte todas las construcciones recibidas.
void KeepLastCharacterPerSlot(Protocol::FrontEnd::CharacterList& list)
{
    std::vector<Protocol::FrontEnd::CharacterEntry> unique;
    unique.reserve(list.characters.size());
    for (auto entry = list.characters.rbegin(); entry != list.characters.rend(); ++entry)
    {
        if (std::ranges::find(unique, entry->slot, &Protocol::FrontEnd::CharacterEntry::slot) == unique.end())
        {
            unique.push_back(*entry);
        }
    }
    std::ranges::reverse(unique);
    list.characters = std::move(unique);
}

// 0x4aec13..0x4aec8d: CreateFileA para leer, GetFileSize y un ReadFile del archivo entero. Vacío si no
// se abre (el CreateFileA que falla en 0x4aec26: tampoco abre una carpeta) o no se lee entero.
std::optional<std::vector<std::byte>> ReadWholeFile(const std::filesystem::path& path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error))
    {
        return std::nullopt;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad())
    {
        return std::nullopt;
    }
    std::vector<std::byte> image(bytes.size());
    std::ranges::transform(bytes, image.begin(), [](char byte) { return static_cast<std::byte>(byte); });
    return image;
}
} // namespace

ServerSession::ServerSession(Network::Connection& connection, Core::IClock& clock, Core::Log& log,
                             Network::ConnectionKeys keys, SessionSettings settings, ISessionEventSink& eventSink)
    : m_connection(connection), m_clock(clock), m_log(log), m_eventSink(eventSink), m_keys(keys),
      m_settings(std::move(settings)), m_connectServer(log), m_gameServer(log)
{
    RegisterTheRows(m_connectServer, Session::ConnectServerOpcodes());
    RegisterTheRows(m_gameServer, Session::GameServerOpcodes());
    // El timer global no depende de abrir una conexión ni de autenticar (04 § El flujo de conexión).
    m_keepalive.Start(m_clock.Now());
    m_chatRoomPing.Start(m_clock.Now());
}

// A new Dispatcher has nothing and no two rows share a code (EachCodeHasOneRow), so registering cannot fail. The ring
// rows (0x4e3594, 0x4e372e, 0x4e38ca, 0x4e3ace and 0x4e3e98) copy before the flag and the reader, even short.
void ServerSession::RegisterTheRows(Protocol::Dispatcher& dispatcher, std::span<const Session::OpcodeRow> rows)
{
    for (const Session::OpcodeRow& row : rows)
    {
        const auto* const handler = std::get_if<Session::RowHandler>(&row.action);
        if (handler == nullptr)
            continue;
        Protocol::Handler call = [this, &row, handle = *handler](const Network::Packet& packet)
        {
            if (row.copiesToRing && !CheckApplication(m_eventSink.CopyToPacketRing(packet.Bytes())))
                return;
            handle(*this, packet);
        };
        [[maybe_unused]] const bool registered = row.subcode ? dispatcher.On(row.opcode, *row.subcode, std::move(call))
                                                             : dispatcher.On(row.opcode, std::move(call));
        assert(registered && "cada fila se registra una vez");
    }
}

void ServerSession::SendGamePacket(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::Start()
{
    if (m_state == Session::SessionState::QuitOrdered)
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
    m_applicationFailure.reset();
    m_createResult.reset();
    m_deleteResult.reset();
    m_deletingSlot.reset();
    m_chosenSlot.reset();
    m_loginSent = false;
    m_account.clear();
    m_mapServerMove.reset();
    m_log.Info("se conecta al ConnectServer");
    Connect(m_settings.connectServer, std::nullopt);
    SetState(Session::SessionState::ConnectingConnectServer);
}

Core::Expected<void, std::string> ServerSession::Poll()
{
    if (m_applicationFailure)
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    HandleEvents();
    if (m_applicationFailure)
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    PollTheChatRooms();
    if (m_applicationFailure)
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    const Core::Duration now = m_clock.Now();
    // También consume los vencimientos sin GS: no quedan pendientes para la próxima conexión.
    const bool keepaliveDue = m_keepalive.Due(now);
    const bool chatRoomPingDue = m_chatRoomPing.Due(now);
    const std::string_view awaited = Awaited(m_state);
    if (!awaited.empty() && now - m_stateSince >= ReplyTimeout)
    {
        m_log.Error("el {} no contestó en {} s: se esperaba {}", ServerName(),
                    std::chrono::duration_cast<std::chrono::seconds>(ReplyTimeout).count(), awaited);
        Fail(Session::SessionFailure::NoReply);
        return {};
    }
    if (keepaliveDue && m_connection.State() == Network::LinkState::Open && !WithConnectServer())
    {
        SendKeepalive(now);
    }
    if (chatRoomPingDue && !m_chatRooms.empty() && !CheckApplication(m_eventSink.ApplyWorld(ChatRoomPingDue{}, *this)))
    {
        return Core::Unexpected(*m_applicationFailure);
    }
    return {};
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

Session::SessionState ServerSession::State() const
{
    return m_state;
}

// 0x4c4854 escribe el 0x3d sin mirar el estado anterior. Solo lo escribe 0x4c3940: ni 0x4c55a0 (el
// F3 10) ni los cambios de mapa (06 § Los cambios de mapa).
void ServerSession::PublishWorldEntry()
{
    if (!m_applyingWorldEntry)
    {
        m_log.Warning("se ignora la publicación de la entrada al mundo: solo la hace la respuesta del F3 03");
        return;
    }
    m_log.Info("se aplicó la entrada al mundo: la sesión está en el mundo");
    SetState(Session::SessionState::InWorld);
}

bool ServerSession::JoinedWorld() const
{
    return m_state == Session::SessionState::InWorld;
}

std::uint16_t ServerSession::PlayerIndex() const
{
    return m_playerIndex;
}

const Scenes::WorldEntryResources* ServerSession::RoutedWorld() const
{
    return m_eventSink.RoutedWorld();
}

Core::Log& ServerSession::Log() const
{
    return m_log;
}

void ServerSession::IgnoreUnreadable(std::string_view ignored)
{
    m_log.Warning("{}: {}", ignored, Unreadable);
}

void ServerSession::ReportInClear(std::string_view warning, Protocol::Outgoing report)
{
    m_log.Warning("{}: se le avisa al GameServer (F1 03)", warning);
    m_connection.Send(std::move(report.packet), report.delivery);
}

void ServerSession::FinishRoutedReception(Core::Expected<void, std::string> applied)
{
    m_eventSink.AfterWorldReception(*this);
    static_cast<void>(CheckApplication(std::move(applied)));
}

std::uint32_t ServerSession::StartCount() const
{
    return m_starts;
}

std::span<const Protocol::Session::ServerEntry> ServerSession::ServerList() const
{
    return m_serverList;
}

// 0x645e10 manda el F4 03 con el código del botón y cierra la lista; el log no dice cuál.
void ServerSession::ChooseServer(std::uint16_t code)
{
    if (m_state != Session::SessionState::ChoosingServer)
    {
        m_log.Warning("se ignora la elección de un servidor: {}", NotAwaited);
        return;
    }
    m_log.Info("se elige un servidor: se pide su dirección");
    m_chosenServer = code;
    Protocol::Outgoing choice = Protocol::Session::WriteServerSelect(code);
    m_connection.Send(std::move(choice.packet), choice.delivery);
    SetState(Session::SessionState::WaitingServerAddress);
}

std::optional<std::uint16_t> ServerSession::ChosenServer() const
{
    return m_chosenServer;
}

// El tick es el de GetTickCount en el original (0x641bf0): el del reloj del bucle en este momento.
// Ni la cuenta ni la clave van al log, tampoco cuando no entran.
void ServerSession::SubmitCredentials(const Credentials& credentials)
{
    if (m_state != Session::SessionState::WaitingCredentials)
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
    const Protocol::Session::LoginRequest request{credentials.account, credentials.password,
                                                  Kernel::TickCountMs(m_clock.Now())};
    auto login = Protocol::Session::WriteLoginRequest(request);
    if (!login.HasValue())
    {
        m_log.Error("no se puede escribir el login: la cuenta o la clave pasan de {} bytes",
                    Protocol::Session::CredentialSize);
        Fail(Session::SessionFailure::CredentialsTooLong);
        return;
    }
    m_log.Info("se manda el login");
    m_loginResult.reset();
    // 0x641cf5..0x641d22: [0x5767928] en 1 y la cuenta en 0x576792c.
    m_loginSent = true;
    m_account = credentials.account;
    Protocol::Outgoing outgoing = std::move(login).Value();
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    SetState(Session::SessionState::WaitingLoginResult);
}

std::optional<Session::SessionFailure> ServerSession::Failure() const
{
    return m_failure;
}

std::optional<Session::ServerAddress> ServerSession::GameServer() const
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
    if (m_state != Session::SessionState::LoggedIn)
    {
        m_log.Warning("se ignora el pedido de la lista de personajes: {}", NotAwaitedMasculine);
        return;
    }
    m_log.Info("se pide la lista de personajes");
    Protocol::Outgoing request = Protocol::FrontEnd::WriteCharacterListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(Session::SessionState::WaitingCharacterList);
}

const std::optional<Protocol::FrontEnd::CharacterList>& ServerSession::Characters() const
{
    return m_characters;
}

void ServerSession::ChooseCharacter(std::optional<std::uint8_t> slot)
{
    if (m_state != Session::SessionState::ChoosingCharacter)
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

const Protocol::FrontEnd::CharacterEntry* ServerSession::ChosenCharacter() const
{
    if (!m_characters || !m_chosenSlot)
    {
        return nullptr;
    }
    const auto& characters = m_characters->characters;
    const auto chosen = std::ranges::find(characters, *m_chosenSlot, &Protocol::FrontEnd::CharacterEntry::slot);
    return chosen == characters.end() ? nullptr : &*chosen;
}

// 0x636290 manda el F3 03 en el primer paso de la escena 5 y loguea el lugar y el nombre; acá, el
// lugar: el nombre es del jugador.
void ServerSession::EnterWorld()
{
    if (m_state != Session::SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora la entrada al mundo: {}", NotAwaited);
        return;
    }
    const Protocol::FrontEnd::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se entra al mundo: no hay un personaje elegido");
        return;
    }
    m_log.Info("se entra al mundo con el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing select = Protocol::FrontEnd::WriteCharacterSelect(chosen->name);
    m_connection.Send(std::move(select.packet), select.delivery);
    SetState(Session::SessionState::EnteringWorld);
}

// 0x63d990 manda el F3 01 después de sus chequeos del nombre, que son de la ventana: acá solo se mira
// que entre en el paquete. El nombre es del jugador y no va al log.
void ServerSession::CreateCharacter(std::string_view name, std::uint8_t characterClass)
{
    if (m_state != Session::SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de crear un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const std::optional<std::array<std::byte, Protocol::FrontEnd::NameSize>> bytes =
        Protocol::FrontEnd::NameFromText(name);
    if (!bytes)
    {
        m_log.Warning("no se crea el personaje: el nombre no entra en 10 bytes");
        return;
    }
    m_log.Info("se pide crear un personaje de la clase {}", characterClass);
    Protocol::Outgoing create = Protocol::FrontEnd::WriteCharacterCreate(*bytes, characterClass);
    m_connection.Send(std::move(create.packet), create.delivery);
    m_createResult.reset();
    SetState(Session::SessionState::WaitingCreateResult);
}

std::optional<std::uint8_t> ServerSession::CreateResult() const
{
    return m_createResult;
}

// 0x643740 manda el F3 02 con el nombre del elegido y el campo de la ventana. El código es como la
// clave: no va al log.
void ServerSession::DeleteCharacter(std::span<const std::byte, Protocol::FrontEnd::DeleteCodeSize> code)
{
    if (m_state != Session::SessionState::ChoosingCharacter)
    {
        m_log.Warning("se ignora el pedido de borrar un personaje: {}", NotAwaitedMasculine);
        return;
    }
    const Protocol::FrontEnd::CharacterEntry* chosen = ChosenCharacter();
    if (chosen == nullptr)
    {
        m_log.Warning("no se borra un personaje: no hay uno elegido");
        return;
    }
    m_log.Info("se pide borrar el personaje del lugar {}", chosen->slot);
    Protocol::Outgoing request = Protocol::FrontEnd::WriteCharacterDelete(chosen->name, code);
    m_connection.Send(std::move(request.packet), request.delivery);
    m_deletingSlot = chosen->slot;
    m_deleteResult.reset();
    SetState(Session::SessionState::WaitingDeleteResult);
}

void ServerSession::RequestLogout(std::uint8_t kind)
{
    if (m_state != Session::SessionState::ChoosingCharacter && m_state != Session::SessionState::InWorld)
    {
        m_log.Warning("se ignora el pedido de salida (F1 02 con {}): no hay un GameServer que lo espere", kind);
        return;
    }
    m_log.Info("se pide la salida al GameServer (F1 02 con {})", kind);
    Protocol::Outgoing request = Protocol::Session::WriteLogoutRequest(kind);
    m_connection.Send(std::move(request.packet), request.delivery);
}

std::optional<std::uint8_t> ServerSession::DeleteResult() const
{
    return m_deleteResult;
}

// Al CS, lo primero es el A9, como lo manda 0x415be0 apenas la escena de login conecta (Q-C16). El
// GS saluda primero: al abrirlo no sale nada.
void ServerSession::OnOpened()
{
    if (m_state == Session::SessionState::ConnectingConnectServer)
    {
        m_log.Info("conectó con el ConnectServer");
        Protocol::Outgoing request = Protocol::Pets::WritePetInfoRequest(Protocol::Pets::LoginScenePetInfoRequest);
        m_connection.Send(std::move(request.packet), request.delivery);
        SetState(Session::SessionState::WaitingConnectHello);
        return;
    }
    m_log.Info("conectó con el GameServer: se espera su saludo");
    SetState(Session::SessionState::WaitingGameHello);
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
    Protocol::Outgoing report = Protocol::Session::WriteDecryptErrorReport();
    m_connection.Send(std::move(report.packet), report.delivery);
}

void ServerSession::OnClosed(Network::CloseReason reason)
{
    m_log.Error("se perdió la conexión con el {}: {}", ServerName(), Network::CloseReasonText(reason));
    Fail(WithConnectServer() ? Session::SessionFailure::ConnectServerLost : Session::SessionFailure::GameServerLost);
}

// 0x4e33e4 pide la lista con cada saludo: en 8 de 64 conexiones sale dos veces (04 § El flujo de
// conexión). Acá, una vez.
void ServerSession::OnConnectHello([[maybe_unused]] const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingConnectHello)
    {
        m_log.Warning("se ignora el saludo del ConnectServer (00): {}", OutOfTurn);
        return;
    }
    m_log.Info("el ConnectServer saludó: se pide la lista de servidores");
    Protocol::Outgoing request = Protocol::Session::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(Session::SessionState::WaitingServerList);
}

// La lista se guarda en el orden en que llega, y la elección es de la UI (CL5.5) o del AutoLogin,
// que elige el primero (pregunta 9 del plan de la CL4). 0x4c1860 la reparte en grupos para la
// ventana: eso es de FrontEnd::GroupServers.
void ServerSession::OnServerList(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingServerList)
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", OutOfTurn);
        return;
    }
    const auto list = Protocol::Session::ReadServerList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de servidores (F4 06): {}", Unreadable);
        return;
    }
    const std::vector<Protocol::Session::ServerEntry>& entries = list.Value();
    if (entries.empty())
    {
        m_log.Error("la lista de servidores está vacía: no hay a qué GameServer conectarse");
        Fail(Session::SessionFailure::EmptyServerList);
        return;
    }
    m_log.Info("la lista trae {} servidores: se espera la elección", entries.size());
    m_serverList = entries;
    SetState(Session::SessionState::ChoosingServer);
}

// El original loguea la dirección, cierra el CS (0x4e8380) y conecta al GS (0x4c1340). Acá el log
// no la dice: es del servidor, como el host de la config.
void ServerSession::OnServerAddress(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", OutOfTurn);
        return;
    }
    const auto address = Protocol::Session::ReadServerAddress(packet);
    if (!address.HasValue())
    {
        m_log.Warning("se ignora la dirección del GameServer (F4 03): {}", Unreadable);
        return;
    }
    m_log.Info("el ConnectServer dio la dirección del GameServer: se cierra y se conecta a ese");
    m_gameServerAddress = Session::ServerAddress{address.Value().host, address.Value().port};
    Connect(*m_gameServerAddress, m_keys);
    SetState(Session::SessionState::ConnectingGameServer);
}

// El F4 05 contesta la elección de un servidor ocupado. Como el original (0x4e2b10 llama a
// 0x4c1bf0, que manda C1 04 F4 02), la sesión vuelve a pedir la lista al mismo CS, para que el
// jugador elija otro (Q-C71). El byte que sigue al subcódigo, el servidor, no se lee: el original
// tampoco lo mira.
void ServerSession::OnServerBusy(const Network::Packet&)
{
    if (m_state != Session::SessionState::WaitingServerAddress)
    {
        m_log.Warning("se ignora el aviso de servidor ocupado (F4 05): {}", OutOfTurn);
        return;
    }
    m_log.Warning("el servidor elegido está ocupado: se pide la lista otra vez");
    m_chosenServer.reset();
    Protocol::Outgoing request = Protocol::Session::WriteServerListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    SetState(Session::SessionState::WaitingServerList);
}

// 0x4c1e20 muestra "Connectting error" con otro resultado y "Version dismatch" con otra versión,
// aunque el resultado tampoco sea 1. Acá el resultado se mira primero: un saludo rechazado no
// llega a comparar la versión.
void ServerSession::OnGameHello(const Network::Packet& packet)
{
    if (m_loginSent)
    {
        OnGameHelloAfterLogin(packet);
        return;
    }
    if (m_state != Session::SessionState::WaitingGameHello)
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", OutOfTurn);
        return;
    }
    const auto hello = Protocol::Session::ReadServerHello(packet);
    if (!hello.HasValue())
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", Unreadable);
        return;
    }
    if (hello.Value().result != Protocol::Session::HelloAccepted)
    {
        m_log.Error("el GameServer rechazó la conexión (resultado {})", hello.Value().result);
        Fail(Session::SessionFailure::HelloRejected);
        return;
    }
    // 0x4c1ec0 guarda la clave con el resultado 1, antes de comparar la versión.
    m_playerIndex = hello.Value().playerIndex;
    const std::string_view version(hello.Value().version.data(), hello.Value().version.size());
    if (version != Protocol::Session::ClientVersion)
    {
        m_log.Error("el GameServer tiene la versión {} y este cliente la {}: no se manda el login",
                    VersionText(hello.Value().version), Protocol::Session::ClientVersion);
        Fail(Session::SessionFailure::VersionMismatch);
        return;
    }
    // 0x4c1e20 abre la ventana de la cuenta con el GS ya conectado (DAT_057678c0 en 2).
    m_log.Info("el GameServer saludó: se espera la cuenta y la clave");
    SetState(Session::SessionState::WaitingCredentials);
}

// 0x4c1e2c..0x4c1e59: sin mirar el resultado, la versión ni el estado, la clave del jugador (0x57678bc), 0x412bb0 y
// 0x447ae0. 0x412bb0 sale sin nada si no hay un B1 00 pendiente; con uno, 0x510f70(−1) y 0x4c2480 (el mundo, con sus
// dos C1 03 31) y el B1 01 con la cuenta, el nombre del elegido (+0xc del objeto de 0x6eed7c, o "" de 0x6b85ec), los
// códigos y el tick, cifrado como C3. 0x447ae0 vacía las 2000 entradas de la caché de las marcas de los gremios
// (0x787aa48, de a 0x58) y +4 de 0x723c58, conservando los nombres y las marcas.
void ServerSession::OnGameHelloAfterLogin(const Network::Packet& packet)
{
    // 0x4c1e2c: este camino no lee ni el resultado ni la versión, sólo pkt[5..6].
    const auto bytes = packet.Bytes();
    if (bytes.size() < 7)
    {
        m_log.Warning("se ignora el saludo del GameServer (F1 00): {}", Unreadable);
        return;
    }
    m_playerIndex = static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(bytes[5]) << 8u) |
                                               std::to_integer<std::uint16_t>(bytes[6]));
    if (!m_mapServerMove)
    {
        static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(GuildTableReset{}, *this)));
        return;
    }
    const Protocol::FrontEnd::CharacterEntry* chosen = ChosenCharacter();
    const std::string name = chosen != nullptr ? Network::TextField(chosen->name) : std::string{};
    if (!CheckApplication(m_eventSink.ApplyWorld(MapServerJoined{}, *this)))
        return;
    // 0x4133cb: el reloj se lee después del reseteo y sus dos cierres, antes del relleno y el serial.
    auto auth = Protocol::Session::WriteMapServerAuth(
        Protocol::Session::MapServerAuth{.account = m_account,
                                         .character = name,
                                         .codes = m_mapServerMove->codes,
                                         .tick = Kernel::TickCountMs(m_clock.Now())});
    if (!auth.HasValue())
    {
        m_log.Error("no se puede escribir el B1 01: la cuenta pasa de 10 bytes");
        Fail(Session::SessionFailure::CredentialsTooLong);
        return;
    }
    if (!CheckApplication(m_eventSink.ApplyWorld(MapServerAuthenticationPrepared{}, *this)))
        return;
    m_log.Info("el GameServer nuevo saludó: se le presenta el personaje (B1 01)");
    Protocol::Outgoing outgoing = std::move(auth).Value();
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    if (!CheckApplication(m_eventSink.ApplyWorld(GuildTableReset{}, *this)))
        return;
    SetState(Session::SessionState::EnteringWorld);
}

// 0x412b30: guarda el movimiento (0x412aa0) y, con [0x5767928] distinto de 0, cierra el GameServer (0x4c1680), guarda
// las opciones y las macros (el mundo), espera 20 ms (Sleep) y conecta con el host y el puerto del paquete (0x4c1340,
// que pone [0x5767918] en 1 si conectó). En cualquier estado, como el original.
void ServerSession::OnMapServerMove(const Network::Packet& packet)
{
    auto move = Protocol::Session::ReadMapServerMove(packet);
    if (!move.HasValue())
    {
        m_log.Warning("se ignora el cambio de servidor del mapa (B1 00): {}", Unreadable);
        return;
    }
    m_mapServerMove = std::move(move).Value();
    if (!m_loginSent)
    {
        m_log.Info("llegó un cambio de servidor del mapa (B1 00) antes del login: se guarda");
        return;
    }
    m_connection.Close();
    ++m_generation;
    if (!CheckApplication(m_eventSink.ApplyWorld(MapServerLeft{}, *this)))
    {
        return;
    }
    m_clock.SleepFor(std::chrono::milliseconds(20)); // 0x412b72: Sleep(20), aun antes de leer el host.
    if (!m_mapServerMove->hostEnds)
    {
        // Sin un 0 en los 0x24 bytes, 0x4c1340 lee el host fuera del objeto (Q-C1402): no se conecta, y el socket ya
        // está cerrado como en el original.
        m_log.Warning("el cambio de servidor del mapa (B1 00) trae un host sin terminar: no se conecta (Q-C1402)");
        Fail(Session::SessionFailure::GameServerLost);
        return;
    }
    m_log.Info("el GameServer manda a otro servidor del mapa (B1 00): se cierra y se conecta a ese");
    Connect(Session::ServerAddress{m_mapServerMove->host, m_mapServerMove->port}, m_keys);
    SetState(Session::SessionState::ConnectingGameServer);
}

void ServerSession::OnLoginResult(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingLoginResult)
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", OutOfTurn);
        return;
    }
    const auto result = Protocol::Session::ReadLoginResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora el resultado del login (F1 01): {}", Unreadable);
        return;
    }
    m_loginResult = result.Value();
    if (result.Value() != Protocol::Session::LoginAccepted)
    {
        // El GS queda abierto: el aviso de la escena, y otra vez la cuenta (0x643560).
        m_log.Warning("el GameServer rechazó el login (resultado {}): se espera otra vez la cuenta y la clave",
                      result.Value());
        SetState(Session::SessionState::WaitingCredentials);
        return;
    }
    m_log.Info("el GameServer aceptó el login");
    SetState(Session::SessionState::LoggedIn);
    // 0x4ad100 manda uno inmediato, sin reiniciar el timer 1000 (04 § El flujo de conexión).
    SendKeepalive(m_clock.Now());
}

// La lista se guarda en el orden en que llega, aunque venga vacía: el original pasa igual al estado
// 0x33 (0x4c20a0). Elegir es del jugador.
void ServerSession::OnCharacterList(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingCharacterList)
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", OutOfTurn);
        return;
    }
    auto list = Protocol::FrontEnd::ReadCharacterList(packet);
    if (!list.HasValue())
    {
        m_log.Warning("se ignora la lista de personajes (F3 00): {}", Unreadable);
        return;
    }
    auto received = std::move(list).Value();
    Protocol::FrontEnd::CharacterList view = received;
    KeepLastCharacterPerSlot(view);
    m_characters = std::move(view);
    m_chosenSlot.reset();
    if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterListReceived{std::move(received)}, *this)))
    {
        return;
    }
    m_log.Info("la lista trae {} personajes: se espera la elección", m_characters->characters.size());
    SetState(Session::SessionState::ChoosingCharacter);
}

// 0x4c2290: con 1, el nuevo va a su lugar con su nombre, su nivel y su clase, sin equipo (0x517270,
// Q-C103), y pisa al que estuviera ahí; con 0 o 2, un aviso que vuelve a abrir la ventana. Con otro,
// el original no hace nada y se queda esperando con el aviso 0x5d abierto (Q-C114): acá, también, y
// el tope de la espera cierra la sesión. Un lugar de más de 4 no tiene posición en el original
// (0x4c2290 deja la de la pila): no se suma.
void ServerSession::OnCreateResult(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingCreateResult)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::FrontEnd::ReadCharacterCreateResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): {}", Unreadable);
        return;
    }
    const Protocol::FrontEnd::CharacterCreateResult& created = read.Value();
    if (created.result != Protocol::FrontEnd::CharacterCreated && created.result != Protocol::FrontEnd::CreateRefused &&
        created.result != Protocol::FrontEnd::CreateRefusedOther)
    {
        m_log.Warning("se ignora la respuesta de crear un personaje (F3 01): el resultado {} no lo atiende el original",
                      created.result);
        return;
    }
    m_createResult = created.result;
    if (created.result == Protocol::FrontEnd::CharacterCreated)
    {
        if (created.slot > Protocol::FrontEnd::MaxCharacterSlot || !m_characters)
        {
            m_log.Warning("el personaje creado no se suma: el lugar {} no está en la selección", created.slot);
        }
        else
        {
            Protocol::FrontEnd::CharacterEntry entry;
            entry.slot = created.slot;
            entry.name = created.name;
            entry.level = created.level;
            entry.classByte = created.classByte;
            entry.withoutEquipment = true;
            entry.guildStatus = Protocol::FrontEnd::NoGuildStatus;
            std::vector<Protocol::FrontEnd::CharacterEntry>& characters = m_characters->characters;
            const auto same = std::ranges::find(characters, entry.slot, &Protocol::FrontEnd::CharacterEntry::slot);
            if (same != characters.end())
            {
                *same = entry;
            }
            else
            {
                characters.push_back(entry);
            }
            if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterCreated{entry}, *this)))
            {
                return;
            }
            m_log.Info("se creó el personaje del lugar {}", entry.slot);
        }
    }
    else
    {
        m_log.Info("el GameServer no creó el personaje: {}", created.result);
    }
    SetState(Session::SessionState::ChoosingCharacter);
}

// 0x4e2b10 lee la respuesta en línea y solo abre su aviso; el 0x60 (con 1) borra al elegido cuando
// llega (Q-C104). Acá se saca al que se mandó, y queda sin elegido.
void ServerSession::OnDeleteResult(const Network::Packet& packet)
{
    if (m_state != Session::SessionState::WaitingDeleteResult)
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", OutOfTurn);
        return;
    }
    auto read = Protocol::FrontEnd::ReadCharacterDeleteResult(packet);
    if (!read.HasValue())
    {
        m_log.Warning("se ignora la respuesta de borrar un personaje (F3 02): {}", Unreadable);
        return;
    }
    m_deleteResult = read.Value();
    if (*m_deleteResult == Protocol::FrontEnd::CharacterDeleted && m_characters && m_deletingSlot)
    {
        std::erase_if(m_characters->characters, [this](const Protocol::FrontEnd::CharacterEntry& entry)
                      { return entry.slot == *m_deletingSlot; });
        if (!CheckApplication(m_eventSink.ApplyCharacter(CharacterDeleted{*m_deletingSlot}, *this)))
        {
            return;
        }
        m_chosenSlot.reset();
        m_log.Info("se borró el personaje del lugar {}", *m_deletingSlot);
    }
    else
    {
        m_log.Info("el GameServer no borró el personaje: {}", *m_deleteResult);
    }
    m_deletingSlot.reset();
    SetState(Session::SessionState::ChoosingCharacter);
}

// 04 § La entrada al mundo: filtra los 23 campos wire y entrega al aplicador síncrono.
void ServerSession::OnWorldEntryReply(const Network::Packet& packet)
{
    if (!packet.WasDecrypted())
    {
        // 0x4c396a: sin cifrar no entra al mundo y le avisa al GameServer con el F1 03 00 F3.
        m_log.Warning("la respuesta de entrada al mundo (F3 03) no llegó cifrada: se le avisa al GameServer (F1 03)");
        Protocol::Outgoing report = Protocol::Session::WriteClearWorldEntryReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    auto reply = Protocol::FrontEnd::ReadCharacterSelectResult(packet);
    if (!reply.HasValue())
    {
        m_log.Warning("se ignora la respuesta de entrada al mundo (F3 03): {}", Unreadable);
        return;
    }
    // El sink publica el 0x3d desde acá (PublishWorldEntry); un error después de publicarlo
    // igual termina en ApplicationFailed.
    m_applyingWorldEntry = true;
    Core::Expected<void, std::string> applied = m_eventSink.ApplyWorld(std::move(reply).Value(), *this);
    m_applyingWorldEntry = false;
    static_cast<void>(CheckApplication(std::move(applied)));
}

// 04 § El F3 10, el inventario: conserva el wire y entrega incluso la lista vacía al aplicador.
void ServerSession::OnInventoryReply(const Network::Packet& packet)
{
    if (!packet.WasDecrypted())
    {
        // 0x4c55a0 vacía el inventario antes de mirar el flag, y sin cifrar le avisa al GameServer con el
        // mismo F1 03 00 F3 que el F3 03 (0x4c56ed..0x4c5e3c), sin leer el paquete.
        m_log.Warning("el inventario (F3 10) no llegó cifrado: se vacía y se le avisa al GameServer (F1 03)");
        if (!CheckApplication(m_eventSink.ApplyWorld(ClearInventoryReceived{}, *this)))
        {
            return;
        }
        Protocol::Outgoing report = Protocol::Session::WriteClearWorldEntryReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    auto reply = Protocol::Items::ReadInventoryList(packet);
    if (!reply.HasValue())
    {
        m_log.Warning("se ignora el inventario (F3 10): {}", Unreadable);
        return;
    }
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(std::move(reply).Value(), *this)));
}

Core::Expected<void, std::string> ServerSession::SendItemMove(const Protocol::Items::ItemMove& move)
{
    return SendWorldPacket(Protocol::Items::WriteItemMove(move));
}

Core::Expected<void, std::string> ServerSession::SendPickUp(std::uint16_t index)
{
    return SendWorldPacket(Protocol::Items::WritePickUp(index));
}

Core::Expected<void, std::string> ServerSession::SendDrop(const Protocol::Items::Drop& drop)
{
    return SendWorldPacket(Protocol::Items::WriteDrop(drop));
}

Core::Expected<void, std::string> ServerSession::SendUseItem(const Protocol::Items::UseItem& use)
{
    return SendWorldPacket(Protocol::Items::WriteUseItem(use));
}

void ServerSession::ResendServerListRequest()
{
    m_log.Info("el 00 llegó en el login: se pide la lista de servidores por el GameServer");
    static_cast<void>(SendWorldPacket(Protocol::Session::WriteServerListRequest()));
}

void ServerSession::SendCreditsNotice()
{
    if (m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("no se manda el F4 06 de los créditos: no hay un socket abierto");
        return;
    }
    m_log.Info("los créditos mandan el F4 06 al {}", ServerName());
    Protocol::Outgoing notice = Protocol::Session::WriteCreditsNotice();
    m_connection.Send(std::move(notice.packet), notice.delivery);
}

Core::Expected<void, std::string> ServerSession::SendWorldPacket(Protocol::Outgoing outgoing)
{
    if (WithConnectServer() || m_connection.State() != Network::LinkState::Open)
    {
        m_log.Warning("no se manda el paquete de mundo: el GameServer no está abierto");
        return Core::Unexpected(std::string("el GameServer no está abierto"));
    }
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    return {};
}

Core::Expected<void, std::string> ServerSession::SendWalk(std::span<const Protocol::Movement::Cell> path,
                                                          Protocol::Movement::Cell target, float angle)
{
    auto walk = Protocol::Movement::WriteWalk(path, target, angle);
    if (!walk.HasValue())
    {
        return Core::Unexpected(std::string("el camino o el ángulo del D3 no se puede escribir"));
    }
    return SendWorldPacket(std::move(walk).Value());
}

Core::Expected<void, std::string> ServerSession::SendFacing(std::uint8_t direction)
{
    return SendWorldPacket(Protocol::Movement::WriteAction(direction, Protocol::Movement::FacingAction));
}

void ServerSession::SendAction(std::uint8_t direction, std::uint8_t action)
{
    static_cast<void>(SendWorldPacket(Protocol::Movement::WriteAction(direction, action)));
}

Core::Expected<void, std::string> ServerSession::SendAttack(std::uint16_t key, std::uint8_t action,
                                                            std::uint8_t direction)
{
    return SendWorldPacket(Protocol::Combat::WriteAttack(key, action, direction));
}

Core::Expected<void, std::string> ServerSession::SendElfCell(std::uint8_t x, std::uint8_t y)
{
    return SendWorldPacket(Protocol::Combat::WriteElfCell(x, y));
}

Core::Expected<void, std::string> ServerSession::SendOptionData(const Protocol::Player::OptionBody& body)
{
    return SendWorldPacket(Protocol::Player::WriteOptionData(body));
}

void ServerSession::RequestGate(std::uint8_t gate)
{
    static_cast<void>(SendWorldPacket(Protocol::Movement::WriteGateRequest(gate)));
}

void ServerSession::ConfirmTeleport()
{
    static_cast<void>(SendWorldPacket(Protocol::Movement::WriteTeleportDone()));
}

void ServerSession::RequestCrywolfState()
{
    static_cast<void>(SendWorldPacket(Protocol::Movement::WriteCrywolfState()));
}

void ServerSession::SendChat(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::RequestCastleSiegeState()
{
    static_cast<void>(SendWorldPacket(Protocol::Movement::WriteCastleSiegeState()));
}

Core::Expected<void, std::string> ServerSession::SendNpcWindowPacket(Protocol::Outgoing packet)
{
    return SendWorldPacket(std::move(packet));
}

void ServerSession::RequestPartyList()
{
    static_cast<void>(SendWorldPacket(Protocol::Party::WritePartyListRequest()));
}

void ServerSession::SendCastlePacket(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::SendGuildPacket(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::RequestQuestList()
{
    static_cast<void>(SendWorldPacket(Protocol::Quests::WriteQuestListRequest()));
}

void ServerSession::SendSetQuest(std::uint8_t quest, std::uint8_t state)
{
    static_cast<void>(SendWorldPacket(Protocol::Quests::WriteSetQuest(quest, state)));
}

void ServerSession::SendTalk(std::int16_t key)
{
    static_cast<void>(SendWorldPacket(Protocol::Quests::WriteTalk(key)));
}

void ServerSession::SendWindowClose()
{
    static_cast<void>(SendWorldPacket(Protocol::Session::WriteWindowClose()));
}

Core::Expected<void, std::string> ServerSession::SendEventWindowLeave()
{
    return SendWorldPacket(Protocol::Events::WriteEventWindowLeave());
}

Core::Expected<void, std::string> ServerSession::SendChaosCastleReposition(std::uint8_t x, std::uint8_t y)
{
    return SendWorldPacket(Protocol::Events::WriteChaosCastleReposition(x, y));
}

void ServerSession::SendFriendPacket(Protocol::Outgoing packet)
{
    static_cast<void>(SendWorldPacket(std::move(packet)));
}

void ServerSession::EnableChatRooms(ChatRoomSockets sockets)
{
    m_chatRoomSockets = std::move(sockets);
}

void ServerSession::OpenChatRoom(std::uint32_t room, std::int32_t window, std::string_view address)
{
    if (m_chatRooms.contains(room))
    {
        m_log.Warning("la sala {} del chat con un amigo ya tiene su conexión", room);
        return;
    }
    if (!m_chatRoomSockets.make || m_chatRoomSockets.obfuscation == nullptr)
    {
        m_log.Warning("no se abre la sala {} del chat con un amigo: no hay red para las salas", room);
        return;
    }
    ChatRoomLink link{.window = window, .opened = false, .socket = m_chatRoomSockets.make(), .connection = nullptr};
    link.connection = std::make_unique<Network::Connection>(*link.socket, *m_chatRoomSockets.obfuscation, m_clock,
                                                            m_log, m_chatRoomSockets.paddingSeed);
    link.connection->Open(address, Chat::ChatRoomPort, m_keys);
    m_chatRooms.emplace(room, std::move(link));
}

void ServerSession::SendToChatRoom(std::uint32_t room, Protocol::Outgoing packet)
{
    const auto found = m_chatRooms.find(room);
    if (found == m_chatRooms.end())
    {
        m_log.Warning("no se manda a la sala {} del chat con un amigo: no tiene conexión", room);
        return;
    }
    // La sala que cerró su conexión sigue anotada (el FD_CLOSE de 0x49e480 no la saca) con el socket en −1: el ping de
    // cada 15 s (0x4a6550) la salta sin decir nada (0x4a66b1), y lo que se le mande se pierde igual.
    if (found->second.connection->State() == Network::LinkState::Closed)
        return;
    found->second.connection->Send(std::move(packet.packet), packet.delivery);
}

void ServerSession::CloseChatRoom(std::uint32_t room)
{
    const auto found = m_chatRooms.find(room);
    if (found == m_chatRooms.end())
        return;
    found->second.connection->Close();
    m_chatRooms.erase(found);
}

std::size_t ServerSession::ChatRoomCount() const
{
    return m_chatRooms.size();
}

// 0x49e510: cada sala, en el orden del mapa. Un handler puede cerrar o abrir salas (al soltar un chat): se recorre
// una copia de las salas y se busca cada una antes de usarla.
void ServerSession::PollTheChatRooms()
{
    std::vector<std::uint32_t> rooms;
    rooms.reserve(m_chatRooms.size());
    for (const auto& [room, link] : m_chatRooms)
        rooms.push_back(room);
    for (const std::uint32_t room : rooms)
    {
        auto found = m_chatRooms.find(room);
        if (found == m_chatRooms.end())
            continue;
        Network::Connection* connection = found->second.connection.get();
        const std::vector<Network::Event> events = connection->Poll();
        for (const Network::Event& event : events)
        {
            found = m_chatRooms.find(room);
            if (found == m_chatRooms.end() || found->second.connection.get() != connection)
                break;
            ChatRoomLink& link = found->second;
            if (std::holds_alternative<Network::Opened>(event))
            {
                link.opened = true;
            }
            else if (const auto* packet = std::get_if<Network::Packet>(&event))
            {
                // Los handlers de 0x4e2a60 leen cada paquete por su lugar sin mirar su largo: uno más corto que lo que
                // lee su handler no se aplica, y la sesión sigue (Q-C1011). El aviso no lleva el texto.
                if (!Protocol::Messenger::CheckRoomPacket(*packet).HasValue())
                {
                    m_log.Warning("se ignora el {:02X} de la sala {} del chat con un amigo: {}",
                                  Network::ByteOf(packet->Opcode().value_or(Network::Opcode{})), room, Unreadable);
                    continue;
                }
                if (!CheckApplication(m_eventSink.ApplyWorld(
                        ChatRoomPacket{.room = room, .window = link.window, .packet = *packet}, *this)))
                    return;
            }
            else if (std::holds_alternative<Network::Closed>(event))
            {
                const ChatRoomClosed closed{.room = room, .window = link.window, .opened = link.opened};
                if (!link.opened)
                    m_chatRooms.erase(found);
                if (!CheckApplication(m_eventSink.ApplyWorld(closed, *this)))
                    return;
            }
        }
    }
}

// 0x4c2710 no mira el estado ni la escena para el 0: SendMessageA(WM_DESTROY) en 0x4c391b, y el
// WM_DESTROY cierra el socket con un RST y vacía la cola de paquetes (0x4e8380), y hace
// PostQuitMessage. Acá se acepta en cualquier estado con el GS, como el original, aunque el GS
// solo la manda con el login aceptado (su cuenta corre con Connected >= 2). La conexión se cierra
// sin el Closed, y lo que quede del lote (otro paquete, o el cierre del GS si llegó en la misma
// lectura) no se recorre (Q-C106). El 1 tampoco mira el estado ni la escena (ReturnToSelection).
void ServerSession::OnLogoutResult(const Network::Packet& packet)
{
    const auto result = Protocol::Session::ReadLogoutResult(packet);
    if (!result.HasValue())
    {
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02): {}", Unreadable);
        return;
    }
    m_eventSink.ClearLogoutRequest(); // 0x4c273f, antes del switch y del guard de cifrado.
    if (result.Value() == Protocol::Session::LogoutReturnsToSelection)
    {
        ReturnToSelection(packet.WasDecrypted());
        return;
    }
    if (result.Value() == Protocol::Session::LogoutReturnsToServerList)
    {
        ReturnToServerList(packet.WasDecrypted());
        return;
    }
    if (result.Value() != Protocol::Session::LogoutQuitsTheGame)
    {
        // Con otro resultado, el original no hace nada.
        m_log.Warning("se ignora la orden de salida del GameServer (F1 02 con {}): motivo desconocido", result.Value());
        return;
    }
    m_log.Info("el GameServer ordenó cerrar el juego (F1 02 con 0): se cierra la conexión y el juego, sin aviso");
    m_connection.Close();
    ++m_generation;
    SetState(Session::SessionState::QuitOrdered);
}

void ServerSession::ReturnToServerList(bool encrypted)
{
    // 0x4c275c: en escena 5 la limpieza precede incluso al rechazo por falta de cifrado.
    if (!CheckApplication(m_eventSink.ApplyWorld(ServerListLogoutCleanup{}, *this)))
        return;
    if (!encrypted)
    {
        m_log.Warning("la orden de volver a los servidores (F1 02 con 2) no llegó cifrada: se avisa al GameServer");
        Protocol::Outgoing report = Protocol::Session::WriteClearLogoutReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    // 0x4c2f0a: cierre explícito, sin procesar el Closed ni otros paquetes del mismo lote.
    m_connection.Close();
    ++m_generation;
    if (!CheckApplication(m_eventSink.ApplyWorld(ReturnToServerListReceived{}, *this)))
        return;
    m_characters.reset();
    m_chosenSlot.reset();
    m_createResult.reset();
    m_deleteResult.reset();
    m_deletingSlot.reset();
    m_loginResult.reset();
    SetState(Session::SessionState::Idle); // 0x4c2f40: estado 0; LoginScene::Enter inicia la conexión al CS.
    m_log.Info("el GameServer ordenó volver a los servidores (F1 02 con 2): se cierra el GS y se vuelve al login");
}

// 0x4c2f5b: sin cifrar, el F1 03 00 F1 y nada más (Q-C211). Cifrado, 0x4c3626..0x4c390f: el sink limpia el
// juego y deja la escena 4; el F3 00 sale en 0x4c3653..0x4c38e4 con el estado 0x32, que 0x4c3900 baja
// a 0 (la escena espera la lista mientras esté antes de 0x33: 06, "La vuelta a la selección"); y 0x4c2480
// manda el C1 03 31 de 0x419900 y, con la caja de 0x723c80 vacía, el de 0x4499b0 (Q-C214). El orden en el
// cable es ese; lo del mundo no gasta nada que dependa de los paquetes. La lista vieja y la elección se
// olvidan: la nueva las reemplaza (0x4c20a0), y hasta que llegue no hay qué elegir.
void ServerSession::ReturnToSelection(bool encrypted)
{
    if (!encrypted)
    {
        m_log.Warning("la orden de volver a la selección (F1 02 con 1) no llegó cifrada: se le avisa al GameServer "
                      "(F1 03) y se sigue igual");
        Protocol::Outgoing report = Protocol::Session::WriteClearLogoutReport();
        m_connection.Send(std::move(report.packet), report.delivery);
        return;
    }
    if (!CheckApplication(m_eventSink.ApplyWorld(ReturnToSelectionReceived{}, *this)))
    {
        return;
    }
    m_log.Info("el GameServer ordenó volver a la selección (F1 02 con 1): se pide la lista de personajes");
    m_characters.reset();
    m_chosenSlot.reset();
    Protocol::Outgoing request = Protocol::FrontEnd::WriteCharacterListRequest();
    m_connection.Send(std::move(request.packet), request.delivery);
    // 0x4c3900 precede al reset; sus envíos31 se producen desde el mundo en sus sitios nativos.
    SetState(Session::SessionState::WaitingCharacterList);
    static_cast<void>(CheckApplication(m_eventSink.ApplyWorld(SelectionWorldReset{}, *this)));
}

// 0x4e2b10 → 0x4e16c0, sin guarda de escena, de estado ni del cifrado: la clave, la imagen del
// ejecutable que nombra la línea de comandos (0x4ad4a0) y la suma, que vuelve como C3. El GS la
// espera tres segundos (gObjSecondProc) y si no llega cierra la conexión. El original primero prueba
// abrirlo para escribir (0x4ad51b) y, si puede, arma otro paquete (la protección, fuera de alcance):
// un ejecutable en uso no se abre así en Windows, y MuRet sólo lo lee (Q-C319).
void ServerSession::OnMainCheck(const Network::Packet& packet)
{
    const auto key = Protocol::Session::ReadMainCheckKey(packet);
    if (!key.HasValue())
    {
        m_log.Warning("se ignora la clave de la suma del ejecutable (03): {}", Unreadable);
        return;
    }
    const std::uint16_t table = Protocol::Session::MainCheckTable(key.Value());
    std::uint32_t sum = 0;
    const std::optional<std::vector<std::byte>> image = ReadWholeFile(m_settings.mainCheckFile);
    if (image)
    {
        const auto summed = Protocol::Session::MainCheckSum(*image, table);
        if (!summed.HasValue())
        {
            static_cast<void>(CheckApplication(Core::Unexpected(summed.Error())));
            return;
        }
        sum = summed.Value();
    }
    else
    {
        m_log.Warning("la suma del ejecutable (03) va en 0: no se abre \"{}\" (0x4aec28)",
                      m_settings.mainCheckFile.string());
    }
    Protocol::Outgoing reply = Protocol::Session::WriteMainCheckReply(sum);
    m_connection.Send(std::move(reply.packet), reply.delivery);
    m_log.Info("se contestó la suma del ejecutable (03)");
}

// El tick es el de GetTickCount en el original (0x4bee60), y las velocidades van con su ajuste.
void ServerSession::SendKeepalive(Core::Duration now)
{
    using Protocol::Session::KeepaliveSpeed;
    const Protocol::Session::Keepalive keepalive{Kernel::TickCountMs(now),
                                                 KeepaliveSpeed(m_speeds.attackSpeed, m_speeds.characterFlags),
                                                 KeepaliveSpeed(m_speeds.magicSpeed, m_speeds.characterFlags)};
    Protocol::Outgoing outgoing = Protocol::Session::WriteKeepalive(keepalive);
    m_connection.Send(std::move(outgoing.packet), outgoing.delivery);
    if (m_connection.State() == Network::LinkState::Open)
    {
        // La Connection pudo encolarlo: no afirma que el GS lo haya recibido.
        m_log.Info("se encoló el keepalive para el GameServer");
    }
}

// El CS va sin claves y el GS con las suyas: cada Open vuelve los seriales a 0.
void ServerSession::Connect(const Session::ServerAddress& address, const std::optional<Network::ConnectionKeys>& keys)
{
    m_connection.Open(address.host, address.port, keys);
    ++m_generation;
}

bool ServerSession::CheckApplication(Core::Expected<void, std::string> applied)
{
    if (applied.HasValue())
    {
        return true;
    }
    if (!m_applicationFailure)
    {
        m_applicationFailure = applied.Error();
        Fail(Session::SessionFailure::ApplicationFailed);
    }
    return false;
}

void ServerSession::Fail(Session::SessionFailure failure)
{
    m_connection.Close();
    ++m_generation;
    m_failure = failure;
    SetState(Session::SessionState::Failed);
}

void ServerSession::SetState(Session::SessionState state)
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
    case Session::SessionState::ConnectingConnectServer:
    case Session::SessionState::WaitingConnectHello:
    case Session::SessionState::WaitingServerList:
    case Session::SessionState::ChoosingServer:
    case Session::SessionState::WaitingServerAddress:
        return true;
    case Session::SessionState::Idle:
    case Session::SessionState::ConnectingGameServer:
    case Session::SessionState::WaitingGameHello:
    case Session::SessionState::WaitingCredentials:
    case Session::SessionState::WaitingLoginResult:
    case Session::SessionState::LoggedIn:
    case Session::SessionState::WaitingCharacterList:
    case Session::SessionState::ChoosingCharacter:
    case Session::SessionState::WaitingCreateResult:
    case Session::SessionState::WaitingDeleteResult:
    case Session::SessionState::EnteringWorld:
    case Session::SessionState::InWorld:
    case Session::SessionState::QuitOrdered:
    case Session::SessionState::Failed:
        break;
    }
    return false;
}
} // namespace Client::App
